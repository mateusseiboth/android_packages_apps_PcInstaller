package org.lineageos.pcinstaller.ui

import android.content.Context
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Build
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Downloading
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.FileOpen
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.automirrored.rounded.InsertDriveFile
import androidx.compose.material.icons.rounded.InstallDesktop
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material.icons.rounded.SystemUpdate
import androidx.compose.material.icons.rounded.TaskAlt
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import org.lineageos.pcinstaller.R
import org.lineageos.pcinstaller.core.CatalogEntry
import org.lineageos.pcinstaller.core.DataForm
import org.lineageos.pcinstaller.core.FlowState
import org.lineageos.pcinstaller.core.InstallError
import org.lineageos.pcinstaller.core.InstallRules
import org.lineageos.pcinstaller.core.KeyStep
import org.lineageos.pcinstaller.core.Offer
import org.lineageos.pcinstaller.core.PackageInfo
import org.lineageos.pcinstaller.core.PackageType
import org.lineageos.pcinstaller.core.Partition
import org.lineageos.pcinstaller.core.Patch
import org.lineageos.pcinstaller.core.SystemForm
import org.lineageos.pcinstaller.core.Task

/** What a screen can ask the view model to do. */
class Actions(
    val start: () -> Unit,
    val back: () -> Unit,
    val choosePartition: (Partition) -> Unit,
    val chooseSystem: (SystemForm) -> Unit,
    val chooseData: (DataForm) -> Unit,
    val chooseDataSize: (Long) -> Unit,
    val review: () -> Unit,
    val install: () -> Unit,
    val reboot: () -> Unit,
    val findUpdate: () -> Unit,
    val askUninstall: () -> Unit,
    val cancelUninstall: () -> Unit,
    val uninstall: () -> Unit,
    val prepareKey: () -> Unit,
    /** Opens the file picker for an update or patch file. */
    val pickPackage: () -> Unit,
    val applyPackage: () -> Unit,
    val askRemovePatch: (Patch) -> Unit,
    val cancelRemovePatch: () -> Unit,
    val removePatch: () -> Unit,
    val checkForUpdates: () -> Unit,
    val download: (CatalogEntry) -> Unit,
)

@Composable
private fun BackButton(actions: Actions, busy: Boolean) {
    TextButton(onClick = actions.back, enabled = !busy) { Text(stringResource(R.string.back)) }
}

@Composable
private fun MainButton(text: String, busy: Boolean, enabled: Boolean = true, onClick: () -> Unit) {
    if (busy) BusyIndicator()
    Button(onClick = onClick, enabled = enabled && !busy) { Text(text) }
}

@Composable
private fun ErrorNote(error: InstallError?) {
    error?.let { Note(stringResource(Texts.error(it)), isError = true) }
}

/** shim needs the live source's key to start our GRUB with Secure Boot on: offer it, or say what comes next. */
@Composable
private fun KeyNote(state: FlowState, busy: Boolean, actions: Actions) {
    val step = state.keyStep()
    if (step == KeyStep.QUEUED) Note(stringResource(R.string.key_queued, state.keyPassword!!))
    if (step != KeyStep.NEEDED) return
    Note(stringResource(R.string.key_needed))
    ErrorNote(state.keyError)
    OutlinedButton(onClick = actions.prepareKey, enabled = !busy) { Text(stringResource(R.string.key_prepare)) }
}

// ---- welcome and loading -------------------------------------------------------------------

private class WelcomeTexts(val title: Int, val body: Int, val loading: Int)

/** The live session installs; an installed system opens on its updates. */
private val WELCOME_TEXTS = mapOf(
    true to WelcomeTexts(R.string.welcome_title, R.string.welcome_body, R.string.loading),
    false to WelcomeTexts(R.string.updates_name, R.string.updates_welcome_body, R.string.updates_loading),
)

@Composable
fun WelcomeScreen(actions: Actions, isLive: Boolean, loading: Boolean) {
    val texts = WELCOME_TEXTS.getValue(isLive)
    Page(
        icon = Icons.Rounded.InstallDesktop,
        title = stringResource(texts.title),
        body = stringResource(texts.body),
        actions = {
            if (loading) Text(stringResource(texts.loading), color = MaterialTheme.colorScheme.onSurfaceVariant)
            MainButton(stringResource(R.string.welcome_start), busy = loading, onClick = actions.start)
        },
    )
}

// ---- partitions ----------------------------------------------------------------------------

private val BLOCKED_ICONS = mapOf(
    InstallError.SOURCE_PARTITION to Icons.Rounded.Usb,
    InstallError.BITLOCKER to Icons.Rounded.Lock,
    InstallError.ENCRYPTED to Icons.Rounded.Lock,
)

@Composable
fun PartitionsScreen(state: FlowState, busy: Boolean, actions: Actions) {
    Page(
        icon = Icons.Rounded.Storage,
        title = stringResource(R.string.partitions_title),
        body = stringResource(R.string.partitions_body),
        step = 1,
        actions = {
            BackButton(actions, busy)
            if (busy) BusyIndicator()
        },
    ) {
        ErrorNote(state.error)
        val source = state.source!!
        state.partitions.sortedBy { InstallRules.blockReason(it, source) != null }.forEach { p ->
            PartitionCard(p, InstallRules.blockReason(p, source), enabled = !busy) { actions.choosePartition(p) }
        }
    }
}

@Composable
private fun PartitionCard(p: Partition, blocked: InstallError?, enabled: Boolean, onClick: () -> Unit) {
    val context = LocalContext.current
    val usedFraction = (1f - p.free.toFloat() / p.size.coerceAtLeast(1).toFloat()).coerceIn(0f, 1f)
    Card(
        onClick = onClick,
        enabled = enabled && blocked == null,
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(20.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(BLOCKED_ICONS[blocked] ?: Icons.Rounded.Storage, null, Modifier.size(36.dp),
                tint = MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f).padding(start = 16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(p.label.ifEmpty { stringResource(R.string.partition_no_label) },
                        style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                    FsChip("${Texts.fileSystem(context, p.fs)} · ${p.name}")
                }
                LinearProgressIndicator(
                    progress = { usedFraction },
                    modifier = Modifier.fillMaxWidth().height(8.dp),
                    strokeCap = StrokeCap.Round,
                )
                Text(stringResource(R.string.partition_free, Texts.bytes(context, p.free), Texts.bytes(context, p.size)),
                    style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                PartitionHint(p, blocked)
            }
        }
    }
}

@Composable
private fun PartitionHint(p: Partition, blocked: InstallError?) {
    val hint = blocked?.let { Texts.error(it) } ?: R.string.partition_images_only.takeUnless { p.allowsFolders }
    hint?.let {
        Text(stringResource(it), style = MaterialTheme.typography.bodyMedium,
            color = mapOf(true to MaterialTheme.colorScheme.error, false to MaterialTheme.colorScheme.tertiary)
                .getValue(blocked != null))
    }
}

@Composable
private fun FsChip(text: String) {
    Surface(shape = RoundedCornerShape(8.dp), color = MaterialTheme.colorScheme.secondaryContainer) {
        Text(text, Modifier.padding(horizontal = 10.dp, vertical = 4.dp), style = MaterialTheme.typography.labelLarge)
    }
}

// ---- forms ---------------------------------------------------------------------------------

private val SYSTEM_ICONS = mapOf(SystemForm.IMAGE to Icons.AutoMirrored.Rounded.InsertDriveFile, SystemForm.FOLDER to Icons.Rounded.FolderOpen)
private val SYSTEM_BODIES = mapOf(SystemForm.IMAGE to R.string.system_image_body, SystemForm.FOLDER to R.string.system_folder_body)
private val DATA_ICONS = mapOf(
    DataForm.FOLDER to Icons.Rounded.FolderOpen,
    DataForm.IMAGE to Icons.AutoMirrored.Rounded.InsertDriveFile,
    DataForm.KEEP to Icons.Rounded.Restore,
)
private val DATA_BODIES = mapOf(
    DataForm.FOLDER to R.string.data_folder_body,
    DataForm.IMAGE to R.string.data_image_body,
    DataForm.KEEP to R.string.data_keep_body,
)

@Composable
fun FormsScreen(state: FlowState, busy: Boolean, actions: Actions) {
    val partition = state.selected!!
    val existing = state.existing!!
    Page(
        icon = Icons.Rounded.Tune,
        title = stringResource(R.string.forms_title),
        body = null,
        step = 2,
        actions = {
            BackButton(actions, busy)
            MainButton(stringResource(R.string.next), busy, onClick = actions.review)
        },
    ) {
        ErrorNote(state.error)
        if (existing.hasSystem) Note(stringResource(R.string.forms_update))
        if (!partition.allowsFolders) Note(stringResource(R.string.partition_images_only))

        SectionTitle(stringResource(R.string.system_section))
        val systemForms = InstallRules.systemForms(partition)
        SystemForm.entries.forEach { form ->
            FormChoice(SYSTEM_ICONS.getValue(form), Texts.system(form), SYSTEM_BODIES.getValue(form),
                selected = state.system == form, allowed = form in systemForms) { actions.chooseSystem(form) }
        }

        SectionTitle(stringResource(R.string.data_section))
        val dataForms = InstallRules.dataForms(partition, existing)
        DataForm.entries.filter { it in dataForms || (it != DataForm.KEEP && !existing.hasData) }.forEach { form ->
            FormChoice(DATA_ICONS.getValue(form), Texts.data(form), DATA_BODIES.getValue(form),
                selected = state.data == form, allowed = form in dataForms) { actions.chooseData(form) }
        }
        if (state.data == DataForm.IMAGE) DataSizeSlider(state, actions)
        if (existing.hasSystem) UninstallButton(busy, actions)
        if (state.isConfirmingUninstall) UninstallDialog(actions)
        Gap()
    }
}

@Composable
private fun UninstallButton(busy: Boolean, actions: Actions) {
    OutlinedButton(
        onClick = actions.askUninstall,
        enabled = !busy,
        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
        modifier = Modifier.padding(top = 8.dp),
    ) {
        Icon(Icons.Rounded.DeleteOutline, null)
        Text(stringResource(R.string.uninstall), Modifier.padding(start = 8.dp))
    }
}

@Composable
private fun UninstallDialog(actions: Actions) {
    AlertDialog(
        onDismissRequest = actions.cancelUninstall,
        icon = { Icon(Icons.Rounded.DeleteOutline, null) },
        title = { Text(stringResource(R.string.uninstall_title)) },
        text = { Text(stringResource(R.string.uninstall_body)) },
        confirmButton = {
            TextButton(
                onClick = actions.uninstall,
                colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
            ) { Text(stringResource(R.string.uninstall_confirm)) }
        },
        dismissButton = { TextButton(onClick = actions.cancelUninstall) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun FormChoice(icon: ImageVector, title: Int, body: Int, selected: Boolean, allowed: Boolean, onClick: () -> Unit) {
    val text = stringResource(body).takeIf { allowed } ?: stringResource(R.string.only_on_linux)
    ChoiceCard(icon, stringResource(title), text, selected, allowed, onClick)
}

@Composable
private fun DataSizeSlider(state: FlowState, actions: Actions) {
    val context = LocalContext.current
    val minGib = InstallRules.MIN_DATA / InstallRules.GIB
    val maxGib = (state.maxDataSize() / InstallRules.GIB).coerceAtLeast(minGib)
    Column(Modifier.fillMaxWidth().padding(horizontal = 8.dp)) {
        Text(stringResource(R.string.data_size, Texts.bytes(context, state.dataSize)), style = MaterialTheme.typography.titleSmall)
        if (maxGib > minGib) {
            Slider(
                value = (state.dataSize / InstallRules.GIB).toFloat(),
                onValueChange = { actions.chooseDataSize(it.toLong() * InstallRules.GIB) },
                valueRange = minGib.toFloat()..maxGib.toFloat(),
                steps = (maxGib - minGib - 1).toInt().coerceIn(0, 254),
            )
        }
    }
}

// ---- summary -------------------------------------------------------------------------------

@Composable
fun SummaryScreen(state: FlowState, busy: Boolean, actions: Actions) {
    val context = LocalContext.current
    val p = state.selected!!
    val check = state.check!!
    val titles = mapOf(true to R.string.summary_update_title, false to R.string.summary_title)
    val buttons = mapOf(true to R.string.update, false to R.string.install)
    val dataText = stringResource(Texts.data(state.data)) +
        (" · " + Texts.bytes(context, state.dataSize)).takeIf { state.data == DataForm.IMAGE }.orEmpty()
    Page(
        icon = Icons.Rounded.TaskAlt,
        title = stringResource(titles.getValue(check.isUpdate)),
        body = null,
        step = 3,
        actions = {
            BackButton(actions, busy)
            MainButton(stringResource(buttons.getValue(check.isUpdate)), busy, onClick = actions.install)
        },
    ) {
        Card(shape = RoundedCornerShape(20.dp), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
            Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp)) {
                SummaryRow(stringResource(R.string.summary_partition),
                    "${p.label.ifEmpty { stringResource(R.string.partition_no_label) }} (${p.name}, ${Texts.fileSystem(context, p.fs)})")
                HorizontalDivider()
                SummaryRow(stringResource(R.string.summary_folder), state.folder)
                HorizontalDivider()
                SummaryRow(stringResource(R.string.summary_system), stringResource(Texts.system(state.system)))
                HorizontalDivider()
                SummaryRow(stringResource(R.string.summary_data), dataText)
                HorizontalDivider()
                SummaryRow(stringResource(R.string.summary_space), Texts.bytes(context, check.need))
            }
        }
        Note(stringResource(R.string.summary_keep_files))
        Note(stringResource(R.string.summary_boot))
        if (!check.isUefi) Note(stringResource(R.string.summary_bios))
        KeyNote(state, busy, actions)
    }
}

// ---- progress, done, failed ----------------------------------------------------------------

private val TASK_TITLES = mapOf(
    Task.INSTALL to R.string.installing_title,
    Task.UPDATE to R.string.updating_title,
    Task.REMOVE to R.string.removing_title,
    Task.APPLY_UPDATE to R.string.updating_title,
    Task.APPLY_PATCH to R.string.applying_patch_title,
    Task.REMOVE_PATCH to R.string.removing_patch_title,
    Task.DOWNLOAD to R.string.downloading_title,
)

/** Only the installs and updates from a USB drive need the drive to stay in. */
private val TASK_BODIES = mapOf(
    Task.INSTALL to R.string.installing_body,
    Task.UPDATE to R.string.installing_body,
    Task.REMOVE to R.string.installing_body,
    Task.APPLY_UPDATE to R.string.package_progress_body,
    Task.APPLY_PATCH to R.string.package_progress_body,
    Task.REMOVE_PATCH to R.string.package_progress_body,
    Task.DOWNLOAD to R.string.downloading_body,
)

@Composable
fun InstallingScreen(state: FlowState) {
    Page(icon = Icons.Rounded.Downloading, title = stringResource(TASK_TITLES.getValue(state.task)),
        body = stringResource(TASK_BODIES.getValue(state.task))) {
        Column(Modifier.fillMaxWidth().padding(top = 24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(Texts.step(state.step)), style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                Text("${state.percent}%", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
            }
            LinearProgressIndicator(
                progress = { state.percent / 100f },
                modifier = Modifier.fillMaxWidth().height(12.dp),
                strokeCap = StrokeCap.Round,
            )
        }
    }
}

/** Title and text of the last screen: installed from the USB drive, updated, or removed. */
private val FINISHED_TEXTS = mapOf(
    Task.INSTALL to (R.string.done_title to R.string.done_body),
    Task.UPDATE to (R.string.updated_title to R.string.updated_body),
    Task.REMOVE to (R.string.removed_title to R.string.removed_body),
    Task.APPLY_UPDATE to (R.string.update_ready_title to R.string.update_ready_body),
    Task.APPLY_PATCH to (R.string.patch_ready_title to R.string.patch_ready_body),
    Task.REMOVE_PATCH to (R.string.patch_removed_title to R.string.patch_removed_body),
)

/** The tasks that write GRUB from a USB drive, which may need that drive's key. */
private val KEY_TASKS = setOf(Task.INSTALL, Task.UPDATE)

@Composable
fun DoneScreen(state: FlowState, busy: Boolean, actions: Actions) {
    val (title, body) = FINISHED_TEXTS.getValue(state.task)
    Page(
        icon = Icons.Rounded.CheckCircle,
        title = stringResource(title),
        body = stringResource(body),
        actions = { Button(onClick = actions.reboot) { Text(stringResource(R.string.done_reboot)) } },
    ) {
        if (state.task in KEY_TASKS) KeyNote(state, busy, actions)
    }
}

// ---- an installed system: update files, patches, update from a USB drive ----------------------

/** "27 de nov. de 2026, edição com serviços do Google": the running system in plain words. */
@Composable
private fun installedSystem(state: FlowState): String {
    val running = state.running ?: return state.source!!.installed!!.version
    return Texts.installedSystem(LocalContext.current, running.dateUtc, running.flavor)
}

@Composable
fun UpdateHomeScreen(state: FlowState, busy: Boolean, actions: Actions) {
    Page(
        icon = Icons.Rounded.SystemUpdate,
        title = stringResource(R.string.updates_name),
        body = stringResource(R.string.installed_system, installedSystem(state)),
        actions = { if (busy) BusyIndicator() },
    ) {
        ErrorNote(state.error)
        Button(onClick = actions.checkForUpdates, enabled = !busy) {
            Icon(Icons.Rounded.CloudDownload, null)
            Text(stringResource(R.string.check_for_updates), Modifier.padding(start = 8.dp))
        }
        state.offer?.let { OfferList(it, busy, actions) }

        SectionTitle(stringResource(R.string.updates_file_section))
        Hint(stringResource(R.string.updates_file_body))
        OutlinedButton(onClick = actions.pickPackage, enabled = !busy) {
            Icon(Icons.Rounded.FileOpen, null)
            Text(stringResource(R.string.updates_file_choose), Modifier.padding(start = 8.dp))
        }

        SectionTitle(stringResource(R.string.patches_section))
        state.patches.forEach { PatchCard(it, busy, actions) }
        if (state.patches.isEmpty()) Hint(stringResource(R.string.patches_none))
        OutlinedButton(onClick = actions.pickPackage, enabled = !busy) { Text(stringResource(R.string.patch_apply)) }

        SectionTitle(stringResource(R.string.usb_section))
        Hint(stringResource(R.string.update_body))
        OutlinedButton(onClick = actions.findUpdate, enabled = !busy) {
            Icon(Icons.Rounded.Usb, null)
            Text(stringResource(R.string.update_search), Modifier.padding(start = 8.dp))
        }
        state.patchToRemove?.let { RemovePatchDialog(it, actions) }
        Gap()
    }
}

/** What "Check for updates" found: a newer system and patches for this one, or that it is up to date. */
@Composable
private fun OfferList(offer: Offer, busy: Boolean, actions: Actions) {
    val context = LocalContext.current
    if (offer.isEmpty) Note(stringResource(R.string.up_to_date))
    offer.update?.let {
        OfferCard(Icons.Rounded.SystemUpdate, stringResource(R.string.offer_update_title), null,
            stringResource(R.string.offer_details, it.version, Texts.bytes(context, it.size)), busy) { actions.download(it) }
    }
    offer.patches.forEach {
        OfferCard(Icons.Rounded.Build, it.name, it.description.ifEmpty { null },
            stringResource(R.string.offer_details, it.version, Texts.bytes(context, it.size)), busy) { actions.download(it) }
    }
}

@Composable
private fun OfferCard(icon: ImageVector, title: String, body: String?, details: String, busy: Boolean, onDownload: () -> Unit) {
    Card(
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(icon, null, Modifier.size(28.dp), tint = MaterialTheme.colorScheme.onPrimaryContainer)
            Column(Modifier.weight(1f).padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(title, style = MaterialTheme.typography.titleMedium)
                body?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }
                Text(details, style = MaterialTheme.typography.bodySmall)
            }
            Button(onClick = onDownload, enabled = !busy) { Text(stringResource(R.string.offer_download)) }
        }
    }
}

@Composable
private fun Hint(text: String) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun PatchCard(patch: Patch, busy: Boolean, actions: Actions) {
    Card(
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Rounded.Build, null, Modifier.size(28.dp), tint = MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f).padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(patch.name, style = MaterialTheme.typography.titleMedium)
                if (patch.description.isNotEmpty()) Hint(patch.description)
                Text(stringResource(R.string.patch_version, patch.version), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            TextButton(
                onClick = { actions.askRemovePatch(patch) },
                enabled = !busy,
                colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
            ) { Text(stringResource(R.string.uninstall_confirm)) }
        }
    }
}

@Composable
private fun RemovePatchDialog(patch: Patch, actions: Actions) {
    AlertDialog(
        onDismissRequest = actions.cancelRemovePatch,
        icon = { Icon(Icons.Rounded.DeleteOutline, null) },
        title = { Text(stringResource(R.string.patch_remove_title, patch.name)) },
        text = { Text(stringResource(R.string.patch_remove_body)) },
        confirmButton = {
            TextButton(
                onClick = actions.removePatch,
                colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
            ) { Text(stringResource(R.string.uninstall_confirm)) }
        },
        dismissButton = { TextButton(onClick = actions.cancelRemovePatch) { Text(stringResource(R.string.cancel)) } },
    )
}

private val PACKAGE_TITLES = mapOf(
    PackageType.UPDATE to R.string.package_update_title,
    PackageType.PATCH to R.string.package_patch_title,
    PackageType.UNKNOWN to R.string.package_unknown_title,
)
private val PACKAGE_BUTTONS = mapOf(PackageType.UPDATE to R.string.package_install, PackageType.PATCH to R.string.package_apply)
private val PACKAGE_NOTES = mapOf(PackageType.UPDATE to R.string.package_update_note, PackageType.PATCH to R.string.package_patch_note)

/** The card's label and value rows for each kind of file; empty values are left out. */
private val PACKAGE_ROWS: Map<PackageType, (Context, PackageInfo, String) -> List<Pair<Int, String>>> = mapOf(
    PackageType.UPDATE to { c, info, installed ->
        listOf(
            R.string.update_new_label to info.version,
            R.string.installed_label to installed,
            R.string.package_edition to Texts.edition(c, info.flavor),
            R.string.package_size to Texts.bytes(c, info.size),
        )
    },
    PackageType.PATCH to { c, info, _ ->
        listOf(
            R.string.package_name to info.name,
            R.string.package_description to info.description,
            R.string.package_version to info.version,
            R.string.package_size to Texts.bytes(c, info.size),
        )
    },
    PackageType.UNKNOWN to { _, _, _ -> emptyList() },
)

/** What the chosen update or patch file holds, and the button to apply it when it fits this system. */
@Composable
fun PackageScreen(state: FlowState, busy: Boolean, actions: Actions) {
    val info = state.packageInfo!!
    val blocked = info.blockedBy()
    val rows = PACKAGE_ROWS.getValue(info.type)(LocalContext.current, info, installedSystem(state))
        .filter { it.second.isNotEmpty() }
    Page(
        icon = Icons.Rounded.SystemUpdate,
        title = stringResource(PACKAGE_TITLES.getValue(info.type)),
        body = null,
        actions = {
            BackButton(actions, busy)
            PACKAGE_BUTTONS[info.type]?.takeIf { blocked == null }
                ?.let { MainButton(stringResource(it), busy, onClick = actions.applyPackage) }
        },
    ) {
        if (rows.isNotEmpty()) PackageCard(rows)
        ErrorNote(blocked)
        if (blocked == null) PACKAGE_NOTES[info.type]?.let { Note(stringResource(it)) }
    }
}

@Composable
private fun PackageCard(rows: List<Pair<Int, String>>) {
    Card(shape = RoundedCornerShape(20.dp), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
        Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp)) {
            rows.forEachIndexed { index, (label, value) ->
                if (index > 0) HorizontalDivider()
                SummaryRow(stringResource(label), value)
            }
        }
    }
}

@Composable
fun UpdateFoundScreen(state: FlowState, busy: Boolean, actions: Actions) {
    val update = state.update!!
    Page(
        icon = Icons.Rounded.SystemUpdate,
        title = stringResource(R.string.update_found_title),
        body = null,
        actions = {
            BackButton(actions, busy)
            if (update.isNewer) MainButton(stringResource(R.string.update), busy, onClick = actions.install)
            if (!update.isNewer) MainButton(stringResource(R.string.update_search_again), busy, onClick = actions.findUpdate)
        },
    ) {
        Card(shape = RoundedCornerShape(20.dp), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
            Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp)) {
                SummaryRow(stringResource(R.string.update_installed_label), update.currentVersion)
                HorizontalDivider()
                SummaryRow(stringResource(R.string.update_new_label), update.version)
            }
        }
        Note(stringResource(R.string.update_keeps_data).takeIf { update.isNewer } ?: stringResource(R.string.update_not_newer),
            isError = !update.isNewer)
    }
}

@Composable
fun FailedScreen(state: FlowState, actions: Actions) {
    Page(
        icon = Icons.Rounded.ErrorOutline,
        title = stringResource(R.string.failed_title),
        body = stringResource(Texts.error(state.error ?: InstallError.UNKNOWN)),
        actions = { Button(onClick = actions.start) { Text(stringResource(R.string.failed_retry)) } },
    )
}
