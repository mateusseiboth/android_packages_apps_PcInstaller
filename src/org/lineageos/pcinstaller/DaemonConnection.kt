package org.lineageos.pcinstaller

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.SystemProperties
import java.io.Closeable
import java.io.FileDescriptor
import java.io.IOException
import org.lineageos.pcinstaller.core.InstallerClient
import org.lineageos.pcinstaller.core.Transport

/** One connection to /dev/socket/pc_installd. */
class LocalSocketTransport(private val socket: LocalSocket) : Transport, Closeable {
    private val reader = socket.inputStream.bufferedReader(Charsets.UTF_8)

    override fun send(line: String) {
        socket.outputStream.write(line.toByteArray(Charsets.UTF_8))
        socket.outputStream.flush()
    }

    /**
     * LocalSocket attaches the pending descriptors to every write until they are cleared, so they go with this
     * line's single write and are dropped right after.
     */
    override fun sendWithFile(line: String, file: FileDescriptor) {
        socket.setFileDescriptorsForSend(arrayOf(file))
        try {
            send(line)
        } finally {
            socket.setFileDescriptorsForSend(null)
        }
    }

    override fun readLine(): String? = reader.readLine()

    override fun close() = socket.close()
}

/** Opens a connection per task, starting pc-installd first when it is not running yet. */
object DaemonConnection {
    private const val SERVICE = "pc_installd"
    private const val TRIES = 25
    private const val WAIT_MS = 200L

    /** Runs block with a connected client; null when the service cannot be reached. */
    fun <T> withClient(block: (InstallerClient) -> T): T? {
        val transport = connect() ?: return null
        return transport.use { block(InstallerClient(it)) }
    }

    private fun connect(): LocalSocketTransport? {
        tryConnect()?.let { return it }
        SystemProperties.set("ctl.start", SERVICE)
        repeat(TRIES) {
            Thread.sleep(WAIT_MS)
            tryConnect()?.let { return it }
        }
        return null
    }

    private fun tryConnect(): LocalSocketTransport? {
        val socket = LocalSocket()
        return try {
            socket.connect(LocalSocketAddress(SERVICE, LocalSocketAddress.Namespace.RESERVED))
            LocalSocketTransport(socket)
        } catch (e: IOException) {
            socket.close()
            null
        }
    }
}
