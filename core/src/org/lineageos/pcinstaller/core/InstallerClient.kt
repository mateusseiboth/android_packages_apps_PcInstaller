package org.lineageos.pcinstaller.core

import java.io.FileDescriptor
import java.io.IOException

/** One connection to pc-installd. Implementations block; call them off the main thread. */
interface Transport {
    fun send(line: String)

    /** Sends line with file's descriptor attached to the same write (SCM_RIGHTS), for the package commands. */
    fun sendWithFile(line: String, file: FileDescriptor): Unit = throw IOException("this transport cannot pass files")

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

    fun uninstall(partition: String, folder: String, onProgress: (InstallStep, Int) -> Unit): DaemonResult<Unit> =
        request("UNINSTALL", "part" to partition, "folder" to folder, onLine = { m -> m.reportProgress(onProgress) }) { }

    fun findUpdate(): DaemonResult<UpdateInfo> = request("FIND_UPDATE") { lines -> lines.single("UPDATE").toUpdateInfo() }

    /** Queues the source's key for MokManager; the password the user types at the next start. */
    fun enrollKey(): DaemonResult<String> = request("ENROLL_KEY") { lines -> lines.single("MOK")["password"].orEmpty() }

    /** Reads an update or patch file; the reply's fields are on its OK line. */
    fun packageInfo(file: FileDescriptor): DaemonResult<PackageInfo> =
        request("PACKAGE_INFO", file = file) { lines -> lines.single("OK").toPackageInfo() }

    /** Stages an update or applies a patch; it takes effect on the next start. locale: the app's language tag. */
    fun applyPackage(file: FileDescriptor, locale: String, onProgress: (InstallStep, Int) -> Unit): DaemonResult<Unit> =
        request("APPLY_PACKAGE", "locale" to locale, file = file, onLine = { m -> m.reportProgress(onProgress) }) { }

    fun patches(): DaemonResult<List<Patch>> =
        request("PATCHES") { lines -> lines.filter { it.word == "PATCH" }.map { it.toPatch() } }

    /** Takes effect on the next start. */
    fun removePatch(name: String): DaemonResult<Unit> = request("REMOVE_PATCH", "name" to name) { }

    private fun <T> request(
        word: String,
        vararg fields: Pair<String, Any>,
        file: FileDescriptor? = null,
        onLine: (Message) -> Unit = {},
        build: (List<Message>) -> T,
    ): DaemonResult<T> = try {
        send(LineCodec.format(word, *fields), file)
        readReply(onLine, build)
    } catch (e: IOException) {
        DaemonResult.Failed(InstallError.DAEMON_UNREACHABLE)
    }

    private fun send(line: String, file: FileDescriptor?) {
        if (file == null) return transport.send(line)
        transport.sendWithFile(line, file)
    }

    /** build gets the data lines and then the OK line, which carries fields for some commands. */

    private fun <T> readReply(onLine: (Message) -> Unit, build: (List<Message>) -> T): DaemonResult<T> {
        val lines = mutableListOf<Message>()
        while (true) {
            val m = transport.readLine()?.let(LineCodec::parse) ?: return DaemonResult.Failed(InstallError.DAEMON_UNREACHABLE)
            when (m.word) {
                "OK" -> return DaemonResult.Ok(build(lines + m))
                "ERR", "ERROR" -> return DaemonResult.Failed(InstallError.fromCode(m["code"]))
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
    installed = toRunningInstall().takeIf { flag("installed") },
    secureBoot = flag("secure_boot"), mokEnrolled = flag("mok_enrolled"), mokQueued = flag("mok_queued"),
)

private fun Message.toRunningInstall() = RunningInstall(
    this["install_part"].orEmpty(), this["install_folder"].orEmpty(), SystemForm.fromWire(this["install_system"]),
    this["install_version"].orEmpty(),
)

private fun Message.toUpdateInfo() = UpdateInfo(
    this["source"].orEmpty(), this["version"].orEmpty(), this["current_version"].orEmpty(), flag("newer"),
    mokEnrolled = flag("mok_enrolled"), mokQueued = flag("mok_queued"),
)

private fun Message.toPartition() = Partition(
    name = this["name"].orEmpty(), disk = this["disk"].orEmpty(), fs = this["fs"].orEmpty(),
    label = this["label"].orEmpty(), size = long("size"), free = long("free"), isEsp = flag("esp"),
    isSource = flag("source"), blockedBy = this["reason"].takeUnless { flag("usable") }?.let(InstallError::fromCode),
    allowsFolders = flag("folders"), maxFile = long("max_file"),
)

private fun Message.toInstallState() = InstallState(flag("system"), flag("data"), long("old"))

private fun Message.toPlanCheck() = PlanCheck(this["mode"] == "update", long("need"), long("free"), this["boot"] == "uefi")

private fun Message.toPackageInfo() = PackageInfo(
    type = PackageType.fromWire(this["type"]), version = this["version"].orEmpty(), name = this["name"].orEmpty(),
    description = this["description"].orEmpty(), flavor = this["flavor"].orEmpty(), build = this["build"].orEmpty(),
    currentBuild = this["current_build"].orEmpty(), currentFlavor = this["current_flavor"].orEmpty(),
    hasKernel = flag("kernel"), hasInitrd = flag("initrd"), hasSystem = flag("system"), hasOverlay = flag("overlay"),
    size = long("size"),
)

private fun Message.toPatch() = Patch(
    this["name"].orEmpty(), this["version"].orEmpty(), this["description"].orEmpty(),
    replacesKernel = flag("kernel"), replacesInitrd = flag("initrd"),
)
