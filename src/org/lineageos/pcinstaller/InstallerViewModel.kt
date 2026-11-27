package org.lineageos.pcinstaller

import android.app.Application
import android.os.ParcelFileDescriptor
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.File
import java.io.FileDescriptor
import org.lineageos.pcinstaller.core.CatalogEntry
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
import org.lineageos.pcinstaller.core.Updates
import org.lineageos.pcinstaller.core.fold

/** Opens the update or patch file the person chose; each command gets a fresh descriptor at offset 0. */
typealias PackageOpener = () -> ParcelFileDescriptor?

/** Runs the daemon calls off the main thread and feeds their results to InstallerFlow. */
class InstallerViewModel(application: Application) : AndroidViewModel(application) {
    private var openPackage: PackageOpener? = null

    /** Holds the one file downloaded from the online list, until it is applied or another download starts. */
    private val downloadDir = File(application.filesDir, "downloads")
    private var downloadCancel: DownloadCancel? = null

    private val _state = MutableStateFlow(FlowState())
    val state: StateFlow<FlowState> = _state.asStateFlow()

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private fun on(event: FlowEvent) = _state.update { InstallerFlow.reduce(it, event) }

    private fun runTask(task: () -> FlowEvent) {
        if (!_busy.compareAndSet(expect = false, update = true)) return
        viewModelScope.launch(Dispatchers.IO) {
            on(task())
            _busy.value = false
        }
    }

    private fun runDaemon(onFailed: (InstallError) -> FlowEvent, task: (InstallerClient) -> FlowEvent) = runTask {
        DaemonConnection.withClient(task) ?: onFailed(InstallError.DAEMON_UNREACHABLE)
    }

    fun start() {
        on(FlowEvent.Start)
        runDaemon(FlowEvent::LoadFailed, ::load)
    }

    private fun load(client: InstallerClient): FlowEvent = client.info().fold(
        { source -> client.partitions().fold({ FlowEvent.Loaded(source, it, findPatches(client, source), RunningSystem.read()) }, FlowEvent::LoadFailed) },
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
        runDaemon(FlowEvent::PackageFailed) { client -> readPackageWith(client, open) }
    }

    private fun readPackageWith(client: InstallerClient, open: PackageOpener): FlowEvent =
        usePackage(open, FlowEvent::PackageFailed) { file ->
            client.packageInfo(file).fold(FlowEvent::PackageRead, FlowEvent::PackageFailed)
        }

    /** Fetches the online update list and keeps what it has for this system. */
    fun checkForUpdates() {
        val running = state.value.running ?: return
        runTask {
            OnlineUpdates.fetchCatalog().fold(
                { FlowEvent.OfferFound(Updates.findOffer(it, running, state.value.patches)) },
                FlowEvent::OfferFailed,
            )
        }
    }

    /** Downloads (or resumes) an update or patch from the online list, then reads it like a picked file. */
    fun download(entry: CatalogEntry) {
        if (_busy.value) return
        val cancel = DownloadCancel().also { downloadCancel = it }
        on(FlowEvent.DownloadStarted)
        runTask {
            PackageDownload(entry, downloadDir, cancel) { on(FlowEvent.DownloadProgressed(it)) }.run()
                .fold(::readDownloaded, FlowEvent::DownloadFailed)
        }
    }

    /** Stops the download and keeps its partial file; the task then ends with DOWNLOAD_CANCELLED. */
    fun cancelDownload() {
        val cancel = downloadCancel ?: return
        Thread(cancel::cancel).start()
    }

    /** The app is closing: stop the download thread; the next download resumes the file. */
    override fun onCleared() {
        cancelDownload()
    }

    private fun readDownloaded(file: File): FlowEvent {
        val open: PackageOpener = { ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY) }
        openPackage = open
        return DaemonConnection.withClient { client -> readPackageWith(client, open) }
            ?: FlowEvent.PackageFailed(InstallError.DAEMON_UNREACHABLE)
    }

    /** Stages the update or applies the patch read by readPackage; locale is the app's language tag. */
    fun applyPackage(locale: String) {
        val open = openPackage ?: return
        if (_busy.value || state.value.packageInfo?.blockedBy() != null) return
        on(FlowEvent.PackageStarted)
        runDaemon(FlowEvent::InstallFailed) { client ->
            usePackage(open, FlowEvent::InstallFailed) { file ->
                client.applyPackage(file, locale, ::onProgress).fold({ onApplied() }, FlowEvent::InstallFailed)
            }
        }
    }

    /** pc-installd has copied what it needs: the downloaded file is no longer needed. */
    private fun onApplied(): FlowEvent {
        downloadDir.deleteRecursively()
        return FlowEvent.Installed
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
