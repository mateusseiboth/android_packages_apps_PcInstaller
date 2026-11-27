package org.lineageos.pcinstaller

import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import android.util.JsonReader
import android.util.JsonToken
import java.io.StringReader
import org.lineageos.pcinstaller.core.Catalog
import org.lineageos.pcinstaller.core.CatalogEntry
import org.lineageos.pcinstaller.core.DaemonResult
import org.lineageos.pcinstaller.core.InstallError

/** The online update list (github.com/mateusseiboth/lineage-pc-updates) and the files it points to. */
object OnlineUpdates {
    private const val CATALOG_URL = "https://raw.githubusercontent.com/mateusseiboth/lineage-pc-updates/main/updates.json"
    private const val CONNECT_TIMEOUT_MS = 15_000
    private const val READ_TIMEOUT_MS = 60_000
    private const val BUFFER = 1 shl 16

    /** Fetches and reads updates.json. Blocks; call it off the main thread. */
    fun fetchCatalog(): DaemonResult<Catalog> {
        val text = try {
            open(CATALOG_URL).use { it.bufferedReader().readText() }
        } catch (e: IOException) {
            return DaemonResult.Failed(InstallError.NETWORK)
        }
        // A malformed list throws IOException, IllegalStateException or ClassCastException while it is read.
        return runCatching { parseCatalog(readJson(text) as Map<*, *>) }
            .fold({ DaemonResult.Ok(it) }, { DaemonResult.Failed(InstallError.CATALOG_UNREADABLE) })
    }

    private fun parseCatalog(json: Map<*, *>): Catalog {
        val updates = json["updates"] as? Map<*, *> ?: emptyMap<String, Any>()
        val patches = json["patches"] as? List<*> ?: emptyList<Any>()
        return Catalog(
            updates = updates.entries.associate { (flavor, entry) -> flavor as String to parseEntry(entry as Map<*, *>) },
            patches = patches.map { parseEntry(it as Map<*, *>) },
        )
    }

    private fun parseEntry(json: Map<*, *>): CatalogEntry {
        fun text(key: String) = json[key]?.toString().orEmpty().trim()
        return CatalogEntry(
            name = text("name"), version = text("version"), build = text("build"), description = text("description"),
            url = text("url"), size = text("size").toLongOrNull() ?: 0, sha256 = text("sha256").lowercase(),
        )
    }

    /** JSON into maps, lists, strings (numbers too, unchanged) and booleans. */
    private fun readJson(text: String): Any? = JsonReader(StringReader(text)).use(::readValue)

    private val VALUE_READERS: Map<JsonToken, (JsonReader) -> Any?> = mapOf(
        JsonToken.BEGIN_OBJECT to ::readObject,
        JsonToken.BEGIN_ARRAY to ::readArray,
        JsonToken.STRING to JsonReader::nextString,
        JsonToken.NUMBER to JsonReader::nextString,
        JsonToken.BOOLEAN to JsonReader::nextBoolean,
        JsonToken.NULL to { r -> r.nextNull(); null },
    )

    private fun readValue(reader: JsonReader): Any? = VALUE_READERS.getValue(reader.peek())(reader)

    private fun readObject(reader: JsonReader): Map<String, Any?> {
        val out = mutableMapOf<String, Any?>()
        reader.beginObject()
        while (reader.hasNext()) out[reader.nextName()] = readValue(reader)
        reader.endObject()
        return out
    }

    private fun readArray(reader: JsonReader): List<Any?> {
        val out = mutableListOf<Any?>()
        reader.beginArray()
        while (reader.hasNext()) out += readValue(reader)
        reader.endArray()
        return out
    }

    /**
     * Downloads entry into dir (emptied first, so one file at most is kept), reporting the percent done, and
     * checks its size and sha256 against the list. Blocks; call it off the main thread.
     */
    fun download(entry: CatalogEntry, dir: File, onProgress: (Int) -> Unit): DaemonResult<File> {
        dir.deleteRecursively()
        dir.mkdirs()
        if (entry.size > dir.usableSpace) return DaemonResult.Failed(InstallError.NO_ROOM)
        val file = File(dir, "package.tar")
        val sha256 = try {
            copy(entry, file, onProgress)
        } catch (e: IOException) {
            file.delete()
            return DaemonResult.Failed(InstallError.DOWNLOAD_FAILED)
        }
        if (isIntact(entry, file.length(), sha256)) return DaemonResult.Ok(file)
        file.delete()
        return DaemonResult.Failed(InstallError.DOWNLOAD_DAMAGED)
    }

    /** An empty size or sha256 in the list is not checked; the package's signature is checked when it is read. */
    private fun isIntact(entry: CatalogEntry, size: Long, sha256: String): Boolean =
        (entry.size <= 0 || size == entry.size) && (entry.sha256.isEmpty() || sha256 == entry.sha256)

    /** Copies the download into file and returns its sha256 in hex. */
    private fun copy(entry: CatalogEntry, file: File, onProgress: (Int) -> Unit): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER)
        var done = 0L
        var reported = -1
        open(entry.url).use { input ->
            file.outputStream().use { output ->
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    output.write(buffer, 0, n)
                    digest.update(buffer, 0, n)
                    done += n
                    reported = reportPercent(done, entry.size, reported, onProgress)
                }
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    /** Reports each whole percent once. */
    private fun reportPercent(done: Long, size: Long, reported: Int, onProgress: (Int) -> Unit): Int {
        val percent = (done * 100 / size.coerceAtLeast(1)).toInt().coerceIn(0, 100)
        if (percent != reported) onProgress(percent)
        return percent
    }

    /** Follows redirects (GitHub release assets and SourceForge mirrors); a status other than 200 is an IOException. */
    private fun open(url: String): java.io.InputStream {
        if (!url.startsWith("https://")) throw IOException("not https: $url")
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = CONNECT_TIMEOUT_MS
        connection.readTimeout = READ_TIMEOUT_MS
        connection.instanceFollowRedirects = true
        connection.setRequestProperty("User-Agent", "LineageOS-PC-Updates")
        val status = connection.responseCode
        if (status != HttpURLConnection.HTTP_OK) {
            connection.disconnect()
            throw IOException("HTTP $status")
        }
        return connection.inputStream
    }
}
