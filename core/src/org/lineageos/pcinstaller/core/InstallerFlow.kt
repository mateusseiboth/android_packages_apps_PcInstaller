package org.lineageos.pcinstaller.core

enum class Screen { WELCOME, LOADING, PARTITIONS, FORMS, SUMMARY, INSTALLING, DONE, FAILED }

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
    val step: InstallStep = InstallStep.PREPARE,
    val percent: Int = 0,
    val error: InstallError? = null,
) {
    fun plan() = InstallPlan(selected!!.name, folder, system, data, dataSize)

    fun maxDataSize(): Long = InstallRules.maxDataSize(selected!!, source!!, existing!!)
}

sealed interface FlowEvent {
    data object Start : FlowEvent
    data class Loaded(val source: SourceInfo, val partitions: List<Partition>) : FlowEvent
    data class LoadFailed(val error: InstallError) : FlowEvent
    data class PartitionChosen(val partition: Partition, val state: InstallState) : FlowEvent
    data class InspectFailed(val error: InstallError) : FlowEvent
    data class SystemChosen(val form: SystemForm) : FlowEvent
    data class DataChosen(val form: DataForm) : FlowEvent
    data class DataSizeChosen(val bytes: Long) : FlowEvent
    data class Checked(val check: PlanCheck) : FlowEvent
    data class CheckFailed(val error: InstallError) : FlowEvent
    data object InstallStarted : FlowEvent
    data class Progressed(val step: InstallStep, val percent: Int) : FlowEvent
    data object Installed : FlowEvent
    data class InstallFailed(val error: InstallError) : FlowEvent
    data object Back : FlowEvent
}

/** The installer's screens as a pure state machine; the view model runs the daemon calls and feeds events. */
object InstallerFlow {
    private val PREFERRED_DATA = listOf(DataForm.KEEP, DataForm.FOLDER, DataForm.IMAGE)
    private val BACK = mapOf(
        Screen.PARTITIONS to Screen.WELCOME,
        Screen.FORMS to Screen.PARTITIONS,
        Screen.SUMMARY to Screen.FORMS,
        Screen.FAILED to Screen.WELCOME,
    )

    fun reduce(s: FlowState, e: FlowEvent): FlowState = when (e) {
        FlowEvent.Start -> s.copy(screen = Screen.LOADING, error = null)
        is FlowEvent.Loaded -> onLoaded(s, e)
        is FlowEvent.LoadFailed -> s.copy(screen = Screen.FAILED, error = e.error)
        is FlowEvent.PartitionChosen -> onPartitionChosen(s, e)
        is FlowEvent.InspectFailed -> s.copy(screen = Screen.PARTITIONS, error = e.error)
        is FlowEvent.SystemChosen -> onSystemChosen(s, e.form)
        is FlowEvent.DataChosen -> onDataChosen(s, e.form)
        is FlowEvent.DataSizeChosen -> onDataSizeChosen(s, e.bytes)
        is FlowEvent.Checked -> s.copy(screen = Screen.SUMMARY, check = e.check, error = null)
        is FlowEvent.CheckFailed -> s.copy(screen = Screen.FORMS, error = e.error)
        FlowEvent.InstallStarted -> s.copy(screen = Screen.INSTALLING, step = InstallStep.PREPARE, percent = 0)
        is FlowEvent.Progressed -> s.copy(step = e.step, percent = e.percent)
        FlowEvent.Installed -> s.copy(screen = Screen.DONE, percent = 100)
        is FlowEvent.InstallFailed -> s.copy(screen = Screen.FAILED, error = e.error)
        FlowEvent.Back -> s.copy(screen = BACK[s.screen] ?: s.screen, error = null)
    }

    private fun findLoadError(source: SourceInfo): InstallError? = when {
        !source.live -> InstallError.SOURCE_MISSING
        !source.uefi -> InstallError.NOT_UEFI
        else -> null
    }

    private fun onLoaded(s: FlowState, e: FlowEvent.Loaded): FlowState {
        val loaded = s.copy(source = e.source, partitions = e.partitions)
        val error = findLoadError(e.source) ?: return loaded.copy(screen = Screen.PARTITIONS, error = null)
        return loaded.copy(screen = Screen.FAILED, error = error)
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
