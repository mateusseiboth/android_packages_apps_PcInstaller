package org.lineageos.pcinstaller.core

import java.io.IOException

/** One connection to pc-installd. Implementations block; call them off the main thread. */
interface Transport {
    fun send(line: String)

    /** The next line without its newline, or null when the daemon hung up. */
    fun readLine(): String?
}

class InstallerClient(private val transport: Transport) {

    fun info(): DaemonResult<SourceInfo> = request("INFO") { lines -> lines.single("INFO").toSourceInfo() }

    fun partitions(): DaemonResult<List<Partition>> =
        request("LIST") { lines -> lines.filter { it.word == "PART" }.map { it.toPartition() } }

    fun inspect(partition: String, folder: String): DaemonResult<InstallState> =
        request("INSPECT", "part" to partition, "folder" to folder) { lines -> lines.single("STATE").toInstallState() }

    fun check(plan: InstallPlan): DaemonResult<PlanCheck> =
        request("CHECK", *plan.fields()) { lines -> lines.single("PLAN").toPlanCheck() }

    fun install(plan: InstallPlan, onProgress: (InstallStep, Int) -> Unit): DaemonResult<Unit> =
        request("INSTALL", *plan.fields(), onLine = { m -> m.reportProgress(onProgress) }) { }

    private fun <T> request(
        word: String,
        vararg fields: Pair<String, Any>,
        onLine: (Message) -> Unit = {},
        build: (List<Message>) -> T,
    ): DaemonResult<T> = try {
        transport.send(LineCodec.format(word, *fields))
        readReply(onLine, build)
    } catch (e: IOException) {
        DaemonResult.Failed(InstallError.DAEMON_UNREACHABLE)
    }

    private fun <T> readReply(onLine: (Message) -> Unit, build: (List<Message>) -> T): DaemonResult<T> {
        val lines = mutableListOf<Message>()
        while (true) {
            val m = transport.readLine()?.let(LineCodec::parse) ?: return DaemonResult.Failed(InstallError.DAEMON_UNREACHABLE)
            when (m.word) {
                "OK" -> return DaemonResult.Ok(build(lines))
                "ERR" -> return DaemonResult.Failed(InstallError.fromCode(m["code"]))
            }
            onLine(m)
            lines += m
        }
    }
}

private fun List<Message>.single(word: String): Message = first { it.word == word }

private fun InstallPlan.fields(): Array<Pair<String, Any>> = arrayOf(
    "part" to partition, "folder" to folder, "system" to system.wire, "data" to data.wire, "data_size" to dataSize,
)

private fun Message.reportProgress(onProgress: (InstallStep, Int) -> Unit) {
    if (word != "PROGRESS") return
    onProgress(InstallStep.fromWire(this["step"]), long("percent").toInt())
}

private fun Message.toSourceInfo() = SourceInfo(
    uefi = flag("uefi"), live = flag("live"), folder = this["folder"].orEmpty(),
    kernel = long("kernel"), initrd = long("initrd"), system = long("system"),
)

private fun Message.toPartition() = Partition(
    name = this["name"].orEmpty(), disk = this["disk"].orEmpty(), fs = this["fs"].orEmpty(),
    label = this["label"].orEmpty(), size = long("size"), free = long("free"), isEsp = flag("esp"),
    isSource = flag("source"), blockedBy = this["reason"].takeUnless { flag("usable") }?.let(InstallError::fromCode),
    allowsFolders = flag("folders"), maxFile = long("max_file"),
)

private fun Message.toInstallState() = InstallState(flag("system"), flag("data"), long("old"))

private fun Message.toPlanCheck() = PlanCheck(this["mode"] == "update", long("need"), long("free"), this["esp"].orEmpty())
