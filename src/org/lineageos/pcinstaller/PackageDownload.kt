package org.lineageos.pcinstaller

import android.os.SystemClock
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.security.MessageDigest
import org.lineageos.pcinstaller.core.CatalogEntry
import org.lineageos.pcinstaller.core.DaemonResult
import org.lineageos.pcinstaller.core.DownloadMeter
import org.lineageos.pcinstaller.core.DownloadPhase
import org.lineageos.pcinstaller.core.DownloadProgress
import org.lineageos.pcinstaller.core.InstallError

/** Stops a running download from another thread; the partial file stays for a later resume. */
class DownloadCancel {
    @Volatile
    var isCancelled = false
        private set

    @Volatile
    private var connection: HttpURLConnection? = null

    fun attach(c: HttpURLConnection) {
        connection = c
        if (isCancelled) c.disconnect()
    }

    /** Closes the socket, so call it off the main thread. */
    fun cancel() {
        isCancelled = true
        connection?.disconnect()
    }
}

/**
 * Downloads one file of the online list into dir/package.tar, next to package.source (its url and sha256),
 * so a later download of the same file resumes with a Range request. A mirror that stays slow is left for
 * the original url again, which lets SourceForge pick another one. The size and sha256 are checked over the
 * whole file at the end. Blocks; run it off the main thread.
 */
class PackageDownload(
    private val entry: CatalogEntry,
    private val dir: File,
    private val cancel: DownloadCancel,
    private val onProgress: (DownloadProgress) -> Unit,
) {
    private val file = File(dir, "package.tar")
    private val source = File(dir, "package.source")
    private val total = entry.size
    private var lastReportMs = 0L

    fun run(): DaemonResult<File> {
        keepOnlySamePackage()
        if (total - file.length() > dir.usableSpace) return DaemonResult.Failed(InstallError.NO_ROOM)
        return try {
            fetch()
            check()
        } catch (e: IOException) {
            DaemonResult.Failed(InstallError.DOWNLOAD_CANCELLED.takeIf { cancel.isCancelled } ?: InstallError.DOWNLOAD_FAILED)
        }
    }

    /** The partial file is kept only for the same url and sha256, and never when it is already too long. */
    private fun keepOnlySamePackage() {
        val key = "${entry.url}\n${entry.sha256}\n"
        val isSame = runCatching { source.readText() }.getOrNull() == key && (total <= 0 || file.length() <= total)
        if (isSame) return
        dir.deleteRecursively()
        dir.mkdirs()
        source.writeText(key)
    }

    private fun isComplete(): Boolean = total > 0 && file.length() == total

    /** Each try asks the original url again; the last one stays with whatever mirror it gets. */
    private fun fetch() {
        repeat(MIRROR_TRIES) { attempt ->
            if (isComplete() || fetchOnce(mayLeave = attempt < MIRROR_TRIES - 1)) return
        }
    }

    /** True when the server sent the whole file; false when a slow mirror was left. */
    private fun fetchOnce(mayLeave: Boolean): Boolean {
        val start = file.length()
        report(DownloadPhase.CONNECTING, start, 0, force = true)
        val connection = connect(start)
        try {
            val isResumed = isResumed(connection, start)
            val from = start.takeIf { isResumed } ?: 0L
            return connection.inputStream.use { copy(it, isResumed, from, mayLeave) }
        } finally {
            connection.disconnect()
        }
    }

    private fun connect(start: Long): HttpURLConnection {
        val connection = OnlineUpdates.openConnection(entry.url)
        if (start > 0) connection.setRequestProperty("Range", "bytes=$start-")
        cancel.attach(connection)
        connection.connect()
        return connection
    }

    /** 206 from the bytes asked for continues the file; 200 starts it over; anything else is a failure. */
    private fun isResumed(connection: HttpURLConnection, start: Long): Boolean {
        val status = connection.responseCode
        val isResumed = RESUMES[status] ?: throw refuse(status)
        if (isResumed && rangeStart(connection) != start) throw IOException("unexpected range")
        return isResumed
    }

    /** A range past the server's file means the partial file is not that file's start: drop it. */
    private fun refuse(status: Int): IOException {
        if (status == HTTP_RANGE_NOT_SATISFIABLE) file.delete()
        return IOException("HTTP $status")
    }

    /** "Content-Range: bytes 123-456/789" -> 123. */
    private fun rangeStart(connection: HttpURLConnection): Long? =
        RANGE.find(connection.getHeaderField("Content-Range").orEmpty())?.groupValues?.get(1)?.toLongOrNull()

    private fun copy(input: InputStream, append: Boolean, from: Long, mayLeave: Boolean): Boolean {
        val meter = DownloadMeter()
        val buffer = ByteArray(BUFFER)
        var done = from
        FileOutputStream(file, append).use { output ->
            while (true) {
                if (cancel.isCancelled) throw IOException("cancelled")
                val n = readOrLeave(input, buffer, mayLeave) ?: return false
                if (n < 0) break
                output.write(buffer, 0, n)
                done += n
                val now = SystemClock.elapsedRealtime()
                meter.add(now, done)
                report(DownloadPhase.DOWNLOADING, done, meter.bytesPerSecond())
                if (mayLeave && meter.isSlowTooLong(now)) return false
            }
        }
        if (total > 0 && done < total) throw IOException("stopped at $done of $total")
        return true
    }

    /** A mirror that sends nothing for the read timeout is left like a slow one; null means leave. */
    private fun readOrLeave(input: InputStream, buffer: ByteArray, mayLeave: Boolean): Int? = try {
        input.read(buffer)
    } catch (e: SocketTimeoutException) {
        if (!mayLeave) throw e
        null
    }

    /** Size and sha256 of the whole file; a file that does not match is deleted, so the next try starts over. */
    private fun check(): DaemonResult<File> {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER)
        val size = file.length()
        var done = 0L
        file.inputStream().use { input ->
            while (true) {
                if (cancel.isCancelled) throw IOException("cancelled")
                val n = input.read(buffer)
                if (n < 0) break
                digest.update(buffer, 0, n)
                done += n
                report(DownloadPhase.CHECKING, done, 0, total = size)
            }
        }
        val sha256 = digest.digest().joinToString("") { "%02x".format(it) }
        if (isIntact(size, sha256)) return DaemonResult.Ok(file)
        dir.deleteRecursively()
        return DaemonResult.Failed(InstallError.DOWNLOAD_DAMAGED)
    }

    /** An empty size or sha256 in the list is not checked; the package's signature is checked when it is read. */
    private fun isIntact(size: Long, sha256: String): Boolean =
        (total <= 0 || size == total) && (entry.sha256.isEmpty() || sha256 == entry.sha256)

    /** At most a few reports a second, so the screen moves smoothly without flooding it. */
    private fun report(phase: DownloadPhase, done: Long, speed: Long, total: Long = this.total, force: Boolean = false) {
        val now = SystemClock.elapsedRealtime()
        if (!force && now - lastReportMs < REPORT_EVERY_MS) return
        lastReportMs = now
        onProgress(DownloadProgress(phase, done, total, speed))
    }

    private companion object {
        const val BUFFER = 1 shl 16
        const val MIRROR_TRIES = 4
        const val REPORT_EVERY_MS = 200L
        const val HTTP_RANGE_NOT_SATISFIABLE = 416
        val RESUMES = mapOf(HttpURLConnection.HTTP_OK to false, HttpURLConnection.HTTP_PARTIAL to true)
        val RANGE = Regex("""bytes (\d+)-""")
    }
}
