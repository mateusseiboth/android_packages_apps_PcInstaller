package org.lineageos.pcinstaller

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import android.util.JsonReader
import android.util.JsonToken
import java.io.StringReader
import org.lineageos.pcinstaller.core.Catalog
import org.lineageos.pcinstaller.core.CatalogEntry
import org.lineageos.pcinstaller.core.DaemonResult
import org.lineageos.pcinstaller.core.InstallError

/** The online update list (github.com/mateusseiboth/lineage-pc-updates); PackageDownload fetches its files. */
object OnlineUpdates {
    private const val CATALOG_URL = "https://raw.githubusercontent.com/mateusseiboth/lineage-pc-updates/main/updates.json"
    private const val CONNECT_TIMEOUT_MS = 15_000
    /** A mirror that sends nothing for this long is left for another one (PackageDownload). */
    private const val READ_TIMEOUT_MS = 20_000

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
     * An https connection, not yet sent, that follows redirects (GitHub release assets, SourceForge mirrors)
     * and asks for the bytes as stored, so ranges and lengths count file bytes.
     */
    fun openConnection(url: String): HttpURLConnection {
        if (!url.startsWith("https://")) throw IOException("not https: $url")
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = CONNECT_TIMEOUT_MS
        connection.readTimeout = READ_TIMEOUT_MS
        connection.instanceFollowRedirects = true
        connection.setRequestProperty("User-Agent", "LineageOS-PC-Updates")
        connection.setRequestProperty("Accept-Encoding", "identity")
        return connection
    }

    /** A status other than 200 is an IOException. */
    private fun open(url: String): java.io.InputStream {
        val connection = openConnection(url)
        val status = connection.responseCode
        if (status != HttpURLConnection.HTTP_OK) {
            connection.disconnect()
            throw IOException("HTTP $status")
        }
        return connection.inputStream
    }
}
