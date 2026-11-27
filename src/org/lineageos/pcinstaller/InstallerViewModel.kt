package org.lineageos.pcinstaller

import android.os.ParcelFileDescriptor
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.FileDescriptor
import org.lineageos.pcinstaller.core.DataForm
import org.lineageos.pcinstaller.core.FlowEvent
import org.lineageos.pcinstaller.core.FlowState
import org.lineageos.pcinstaller.core.InstallError
import org.lineageos.pcinstaller.core.InstallerClient
import org.lineageos.pcinstaller.core.InstallerFlow
import org.lineageos.pcinstaller.core.InstallStep
import org.lineageos.pcinstaller.core.Partition
import org.lineageos.pcinstaller.core.Patch
import org.lineageos.pcinstaller.core.SourceInfo
import org.lineageos.pcinstaller.core.SystemForm
import org.lineageos.pcinstaller.core.fold

/** Opens the update or patch file the person chose; each command gets a fresh descriptor at offset 0. */
typealias PackageOpener = () -> ParcelFileDescriptor?

/** Runs the daemon calls off the main thread and feeds their results to InstallerFlow. */
class InstallerViewModel : ViewModel() {
    private var openPackage: PackageOpener? = null

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
        { source -> client.partitions().fold({ FlowEvent.Loaded(source, it, findPatches(client, source)) }, FlowEvent::LoadFailed) },
        FlowEvent::LoadFailed,
    )

    /** Only an installed system has patches; a daemon without PATCHES shows none. */
    private fun findPatches(client: InstallerClient, source: SourceInfo): List<Patch> =
        source.installed?.let { client.patches().fold({ it }, { emptyList() }) }.orEmpty()

    private fun onProgress(step: InstallStep, percent: Int) =
        on(FlowEvent.Progressed(step, percent))

    /** Runs block with the chosen file open, closing it afterwards. */
    private fun usePackage(
        open: PackageOpener,
        onFailed: (InstallError) -> FlowEvent,
        block: (FileDescriptor) -> FlowEvent,
    ): FlowEvent {
        val file = runCatching(open).getOrNull() ?: return onFailed(InstallError.FILE_UNREADABLE)
        return file.use { block(it.fileDescriptor) }
    }

    /** The person picked an update or patch file: ask pc-installd what it is. */
    fun readPackage(open: PackageOpener) {
        openPackage = open
        runDaemon(FlowEvent::PackageFailed) { client ->
            usePackage(open, FlowEvent::PackageFailed) { file ->
                client.packageInfo(file).fold(FlowEvent::PackageRead, FlowEvent::PackageFailed)
            }
        }
    }

    /** Stages the update or applies the patch read by readPackage; locale is the app's language tag. */
    fun applyPackage(locale: String) {
        val open = openPackage ?: return
        if (_busy.value || state.value.packageInfo?.blockedBy() != null) return
        on(FlowEvent.PackageStarted)
        runDaemon(FlowEvent::InstallFailed) { client ->
            usePackage(open, FlowEvent::InstallFailed) { file ->
                client.applyPackage(file, locale, ::onProgress).fold({ FlowEvent.Installed }, FlowEvent::InstallFailed)
            }
        }
    }

    fun askRemovePatch(patch: Patch) = on(FlowEvent.PatchRemoveAsked(patch))

    fun cancelRemovePatch() = on(FlowEvent.PatchRemoveCancelled)

    fun removePatch() {
        val patch = state.value.patchToRemove ?: return
        if (_busy.value) return
        on(FlowEvent.PatchRemoveStarted)
        runDaemon(FlowEvent::InstallFailed) { client ->
            client.removePatch(patch.name).fold({ FlowEvent.Installed }, FlowEvent::InstallFailed)
        }
    }

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
