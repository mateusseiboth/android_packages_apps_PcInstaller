package org.lineageos.pcinstaller.core

/** What a download is doing: waiting for the server, receiving bytes, or checking the finished file. */
enum class DownloadPhase { CONNECTING, DOWNLOADING, CHECKING }

/** A download's progress; while CHECKING, done counts the bytes already checked. */
data class DownloadProgress(
    val phase: DownloadPhase,
    val done: Long,
    val total: Long,
    /** Averaged over the last few seconds; 0 while unknown. */
    val bytesPerSecond: Long,
) {
    val fraction: Float get() = (done.toFloat() / total.coerceAtLeast(1).toFloat()).coerceIn(0f, 1f)

    val percent: Int get() = (fraction * 100).toInt()

    /** null while the speed or the size is unknown. */
    val secondsLeft: Long? get() = ((total - done).coerceAtLeast(0) / bytesPerSecond).takeIf { bytesPerSecond > 0 && total > 0 }
}

/**
 * The speed of one connection over a sliding window, and whether it has stayed slow long enough to try
 * another mirror. Times are milliseconds from any monotonic clock.
 */
class DownloadMeter(
    private val windowMs: Long = 5_000,
    private val slowBytesPerSecond: Long = 200_000,
    private val slowForMs: Long = 20_000,
) {
    private val samples = ArrayDeque<Pair<Long, Long>>()
    private var startedAt: Long? = null
    private var slowSince: Long? = null

    /** bytes: everything received so far. */
    fun add(nowMs: Long, bytes: Long) {
        if (startedAt == null) startedAt = nowMs
        samples.addLast(nowMs to bytes)
        while (samples.size > 2 && nowMs - samples.first().first > windowMs) samples.removeFirst()
    }

    /** Bytes per second over the window; 0 until it spans a second. */
    fun bytesPerSecond(): Long {
        if (samples.size < 2) return 0
        val (t0, b0) = samples.first()
        val (t1, b1) = samples.last()
        if (t1 - t0 < 1_000) return 0
        return (b1 - b0) * 1_000 / (t1 - t0)
    }

    /** True once the speed has stayed under the limit for slowForMs, judged after a first full window. */
    fun isSlowTooLong(nowMs: Long): Boolean {
        val started = startedAt ?: return false
        if (nowMs - started < windowMs || bytesPerSecond() >= slowBytesPerSecond) {
            slowSince = null
            return false
        }
        val since = slowSince ?: nowMs.also { slowSince = it }
        return nowMs - since >= slowForMs
    }
}
