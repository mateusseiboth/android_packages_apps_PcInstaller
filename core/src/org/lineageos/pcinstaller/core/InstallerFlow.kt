package org.lineageos.pcinstaller.core

enum class Screen {
    WELCOME, LOADING, PARTITIONS, FORMS, SUMMARY, INSTALLING, DONE, REMOVED, FAILED, UPDATE_HOME, UPDATE_FOUND, PACKAGE
}

/** The key shim needs to start our GRUB with Secure Boot on: nothing to do, offer it, or queued for MokManager. */
enum class KeyStep { NONE, NEEDED, QUEUED }

/** What the progress screen is doing. */
enum class Task { INSTALL, UPDATE, REMOVE, APPLY_UPDATE, APPLY_PATCH, REMOVE_PATCH }

data class FlowState(
    val screen: Screen = Screen.WELCOME,
    val source: SourceInfo? = null,
    val partitions: List<Partition> = emptyList(),
    val selected: Partition? = null,
    val existing: InstallState? = null,
    val folder: String = "lineage",
    val system: SystemForm = SystemForm.IMAGE,
    val data: DataForm = DataForm.IMAGE,
    val dataSize: Long = InstallRules.MIN_DATA,
    val check: PlanCheck? = null,
    val update: UpdateInfo? = null,
    val task: Task = Task.INSTALL,
    val isConfirmingUninstall: Boolean = false,
    val step: InstallStep = InstallStep.PREPARE,
    val percent: Int = 0,
    val error: InstallError? = null,
    /** Set once the key is queued: MokManager asks for it on the next start. */
    val keyPassword: String? = null,
    val keyError: InstallError? = null,
    /** Installed system: the patches applied to it, the file the person chose, and the patch to remove. */
    val patches: List<Patch> = emptyList(),
    val packageInfo: PackageInfo? = null,
    val patchToRemove: Patch? = null,
) {
    fun keyStep(): KeyStep = when {
        source?.uefi != true -> KeyStep.NONE
        keyPassword != null -> KeyStep.QUEUED
        isKeyEnrolled() -> KeyStep.NONE
        else -> KeyStep.NEEDED
    }

    /** An update installs the USB drive's GRUB, signed with that drive's key. */
    private fun isKeyEnrolled(): Boolean = update?.mokEnrolled ?: source!!.mokEnrolled

    /** An update from a USB drive rewrites the running install as it is; otherwise the user's choices. */
    fun plan(): InstallPlan = update?.let { updatePlan() } ?: InstallPlan(selected!!.name, folder, system, data, dataSize)

    private fun updatePlan(): InstallPlan {
        val running = source!!.installed!!
        return InstallPlan(running.partition, running.folder, running.system, DataForm.KEEP, 0)
    }

    fun maxDataSize(): Long = InstallRules.maxDataSize(selected!!, source!!, existing!!)
}

sealed interface FlowEvent {
    data object Start : FlowEvent
    data class Loaded(val source: SourceInfo, val partitions: List<Partition>, val patches: List<Patch> = emptyList()) :
        FlowEvent
    data class LoadFailed(val error: InstallError) : FlowEvent
    data class PartitionChosen(val partition: Partition, val state: InstallState) : FlowEvent
    data class InspectFailed(val error: InstallError) : FlowEvent
    data class SystemChosen(val form: SystemForm) : FlowEvent
    data class DataChosen(val form: DataForm) : FlowEvent
    data class DataSizeChosen(val bytes: Long) : FlowEvent
    data class Checked(val check: PlanCheck) : FlowEvent
    data class CheckFailed(val error: InstallError) : FlowEvent
    data class UpdateFound(val update: UpdateInfo) : FlowEvent
    data class UpdateMissing(val error: InstallError) : FlowEvent
    data object UninstallAsked : FlowEvent
    data object UninstallCancelled : FlowEvent
    data object UninstallStarted : FlowEvent
    data object InstallStarted : FlowEvent
    data class Progressed(val step: InstallStep, val percent: Int) : FlowEvent
    data object Installed : FlowEvent
    data class InstallFailed(val error: InstallError) : FlowEvent
    data object Back : FlowEvent
    data class KeyQueued(val password: String) : FlowEvent
    data class KeyFailed(val error: InstallError) : FlowEvent
    data class PackageRead(val info: PackageInfo) : FlowEvent
    data class PackageFailed(val error: InstallError) : FlowEvent
    data object PackageStarted : FlowEvent
    data class PatchRemoveAsked(val patch: Patch) : FlowEvent
    data object PatchRemoveCancelled : FlowEvent
    data object PatchRemoveStarted : FlowEvent
}

/** The installer's screens as a pure state machine; the view model runs the daemon calls and feeds events. */
object InstallerFlow {
    private val PREFERRED_DATA = listOf(DataForm.KEEP, DataForm.FOLDER, DataForm.IMAGE)
    private val BACK = mapOf(
        Screen.PARTITIONS to Screen.WELCOME,
        Screen.FORMS to Screen.PARTITIONS,
        Screen.SUMMARY to Screen.FORMS,
        Screen.FAILED to Screen.WELCOME,
        Screen.UPDATE_FOUND to Screen.UPDATE_HOME,
        Screen.PACKAGE to Screen.UPDATE_HOME,
    )
    private val FINISHED = mapOf(
        Task.INSTALL to Screen.DONE, Task.UPDATE to Screen.DONE, Task.REMOVE to Screen.REMOVED,
        Task.APPLY_UPDATE to Screen.DONE, Task.APPLY_PATCH to Screen.DONE, Task.REMOVE_PATCH to Screen.DONE,
    )
    private val PACKAGE_TASKS = mapOf(PackageType.UPDATE to Task.APPLY_UPDATE, PackageType.PATCH to Task.APPLY_PATCH)

    fun reduce(s: FlowState, e: FlowEvent): FlowState = when (e) {
        FlowEvent.Start -> s.copy(screen = Screen.LOADING, error = null, update = null, packageInfo = null)
        is FlowEvent.Loaded -> onLoaded(s, e)
        is FlowEvent.LoadFailed -> s.copy(screen = Screen.FAILED, error = e.error)
        is FlowEvent.PartitionChosen -> onPartitionChosen(s, e)
        is FlowEvent.InspectFailed -> s.copy(screen = Screen.PARTITIONS, error = e.error)
        is FlowEvent.SystemChosen -> onSystemChosen(s, e.form)
        is FlowEvent.DataChosen -> onDataChosen(s, e.form)
        is FlowEvent.DataSizeChosen -> onDataSizeChosen(s, e.bytes)
        is FlowEvent.Checked -> s.copy(screen = Screen.SUMMARY, check = e.check, error = null)
        is FlowEvent.CheckFailed -> s.copy(screen = Screen.FORMS, error = e.error)
        is FlowEvent.UpdateFound -> s.copy(screen = Screen.UPDATE_FOUND, update = e.update, error = null)
        is FlowEvent.UpdateMissing -> s.copy(screen = Screen.UPDATE_HOME, update = null, error = e.error)
        FlowEvent.UninstallAsked -> s.copy(isConfirmingUninstall = s.existing?.hasSystem == true)
        FlowEvent.UninstallCancelled -> s.copy(isConfirmingUninstall = false)
        FlowEvent.UninstallStarted -> startTask(s, Task.REMOVE)
        FlowEvent.InstallStarted -> startTask(s, findTask(s))
        is FlowEvent.Progressed -> s.copy(step = e.step, percent = e.percent)
        FlowEvent.Installed -> s.copy(screen = FINISHED.getValue(s.task), percent = 100)
        is FlowEvent.InstallFailed -> s.copy(screen = Screen.FAILED, error = e.error)
        FlowEvent.Back -> s.copy(screen = BACK[s.screen] ?: s.screen, error = null)
        is FlowEvent.KeyQueued -> s.copy(keyPassword = e.password, keyError = null)
        is FlowEvent.KeyFailed -> s.copy(keyError = e.error)
        is FlowEvent.PackageRead -> s.copy(screen = Screen.PACKAGE, packageInfo = e.info, error = null)
        is FlowEvent.PackageFailed -> s.copy(screen = Screen.UPDATE_HOME, packageInfo = null, error = e.error)
        FlowEvent.PackageStarted -> onPackageStarted(s)
        is FlowEvent.PatchRemoveAsked -> s.copy(patchToRemove = e.patch)
        FlowEvent.PatchRemoveCancelled -> s.copy(patchToRemove = null)
        FlowEvent.PatchRemoveStarted -> startTask(s, Task.REMOVE_PATCH)
    }

    /** A file that does not fit the running system never starts. */
    private fun onPackageStarted(s: FlowState): FlowState {
        val info = s.packageInfo ?: return s
        if (info.blockedBy() != null) return s
        return startTask(s, PACKAGE_TASKS.getValue(info.type))
    }

    private fun findTask(s: FlowState): Task = when {
        s.update != null || s.check?.isUpdate == true -> Task.UPDATE
        else -> Task.INSTALL
    }

    private fun startTask(s: FlowState, task: Task) = s.copy(
        screen = Screen.INSTALLING, task = task, isConfirmingUninstall = false, patchToRemove = null,
        step = InstallStep.PREPARE, percent = 0,
    )

    /** The live USB drive installs; an installed system updates from one; anything else cannot go on. */
    private fun findLoadedScreen(source: SourceInfo): Screen = when {
        source.live -> Screen.PARTITIONS
        source.installed != null -> Screen.UPDATE_HOME
        else -> Screen.FAILED
    }

    private fun onLoaded(s: FlowState, e: FlowEvent.Loaded): FlowState {
        val screen = findLoadedScreen(e.source)
        val error = InstallError.SOURCE_MISSING.takeIf { screen == Screen.FAILED }
        return s.copy(source = e.source, partitions = e.partitions, patches = e.patches, screen = screen, error = error)
    }

    private fun onPartitionChosen(s: FlowState, e: FlowEvent.PartitionChosen): FlowState {
        val dataForms = InstallRules.dataForms(e.partition, e.state)
        val max = InstallRules.maxDataSize(e.partition, s.source!!, e.state)
        return s.copy(
            screen = Screen.FORMS, selected = e.partition, existing = e.state, folder = s.source.folder,
            system = SystemForm.IMAGE, data = PREFERRED_DATA.first { it in dataForms },
            dataSize = InstallRules.defaultDataSize(max).coerceAtLeast(InstallRules.MIN_DATA), error = null,
        )
    }

    private fun onSystemChosen(s: FlowState, form: SystemForm): FlowState {
        if (form !in InstallRules.systemForms(s.selected!!)) return s
        return s.copy(system = form, error = null)
    }

    private fun onDataChosen(s: FlowState, form: DataForm): FlowState {
        if (form !in InstallRules.dataForms(s.selected!!, s.existing!!)) return s
        return s.copy(data = form, error = null)
    }

    private fun onDataSizeChosen(s: FlowState, bytes: Long): FlowState {
        val max = s.maxDataSize().coerceAtLeast(InstallRules.MIN_DATA)
        return s.copy(dataSize = InstallRules.floorGib(bytes).coerceIn(InstallRules.MIN_DATA, max), error = null)
    }
}
