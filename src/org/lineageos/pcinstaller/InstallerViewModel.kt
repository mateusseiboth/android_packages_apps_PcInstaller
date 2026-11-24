package org.lineageos.pcinstaller

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.lineageos.pcinstaller.core.DataForm
import org.lineageos.pcinstaller.core.FlowEvent
import org.lineageos.pcinstaller.core.FlowState
import org.lineageos.pcinstaller.core.InstallError
import org.lineageos.pcinstaller.core.InstallerClient
import org.lineageos.pcinstaller.core.InstallerFlow
import org.lineageos.pcinstaller.core.Partition
import org.lineageos.pcinstaller.core.SystemForm
import org.lineageos.pcinstaller.core.fold

/** Runs the daemon calls off the main thread and feeds their results to InstallerFlow. */
class InstallerViewModel : ViewModel() {
    private val _state = MutableStateFlow(FlowState())
    val state: StateFlow<FlowState> = _state.asStateFlow()

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private fun on(event: FlowEvent) = _state.update { InstallerFlow.reduce(it, event) }

    private fun runDaemon(onFailed: (InstallError) -> FlowEvent, task: (InstallerClient) -> FlowEvent) {
        if (!_busy.compareAndSet(expect = false, update = true)) return
        viewModelScope.launch(Dispatchers.IO) {
            on(DaemonConnection.withClient(task) ?: onFailed(InstallError.DAEMON_UNREACHABLE))
            _busy.value = false
        }
    }

    fun start() {
        on(FlowEvent.Start)
        runDaemon(FlowEvent::LoadFailed, ::load)
    }

    private fun load(client: InstallerClient): FlowEvent = client.info().fold(
        { source -> client.partitions().fold({ FlowEvent.Loaded(source, it) }, FlowEvent::LoadFailed) },
        FlowEvent::LoadFailed,
    )

    fun choosePartition(partition: Partition) = runDaemon(FlowEvent::InspectFailed) { client ->
        client.inspect(partition.name, state.value.folder)
            .fold({ FlowEvent.PartitionChosen(partition, it) }, FlowEvent::InspectFailed)
    }

    fun chooseSystem(form: SystemForm) = on(FlowEvent.SystemChosen(form))

    fun chooseData(form: DataForm) = on(FlowEvent.DataChosen(form))

    fun chooseDataSize(bytes: Long) = on(FlowEvent.DataSizeChosen(bytes))

    fun review() = runDaemon(FlowEvent::CheckFailed) { client ->
        client.check(state.value.plan()).fold(FlowEvent::Checked, FlowEvent::CheckFailed)
    }

    fun install() {
        if (_busy.value) return
        val plan = state.value.plan()
        on(FlowEvent.InstallStarted)
        runDaemon(FlowEvent::InstallFailed) { client ->
            client.install(plan) { step, percent -> on(FlowEvent.Progressed(step, percent)) }
                .fold({ FlowEvent.Installed }, FlowEvent::InstallFailed)
        }
    }

    /** On an installed system: look for a live USB drive with a newer system. */
    fun findUpdate() = runDaemon(FlowEvent::UpdateMissing) { client ->
        client.findUpdate().fold(FlowEvent::UpdateFound, FlowEvent::UpdateMissing)
    }

    /** Queues the live source's key for MokManager on the next start. */
    fun prepareKey() = runDaemon(FlowEvent::KeyFailed) { client ->
        client.enrollKey().fold(FlowEvent::KeyQueued, FlowEvent::KeyFailed)
    }

    fun askUninstall() = on(FlowEvent.UninstallAsked)

    fun cancelUninstall() = on(FlowEvent.UninstallCancelled)

    fun uninstall() {
        if (_busy.value) return
        val partition = state.value.selected!!.name
        val folder = state.value.folder
        on(FlowEvent.UninstallStarted)
        runDaemon(FlowEvent::InstallFailed) { client ->
            client.uninstall(partition, folder) { step, percent -> on(FlowEvent.Progressed(step, percent)) }
                .fold({ FlowEvent.Installed }, FlowEvent::InstallFailed)
        }
    }

    fun back() {
        if (_busy.value) return
        on(FlowEvent.Back)
    }
}
