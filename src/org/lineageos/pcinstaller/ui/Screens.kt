package org.lineageos.pcinstaller.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Downloading
import androidx.compose.material.icons.rounded.ErrorOutline
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
import org.lineageos.pcinstaller.core.DataForm
import org.lineageos.pcinstaller.core.FlowState
import org.lineageos.pcinstaller.core.InstallError
import org.lineageos.pcinstaller.core.InstallRules
import org.lineageos.pcinstaller.core.Partition
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

// ---- welcome and loading -------------------------------------------------------------------

@Composable
fun WelcomeScreen(actions: Actions, loading: Boolean) {
    Page(
        icon = Icons.Rounded.InstallDesktop,
        title = stringResource(R.string.welcome_title),
        body = stringResource(R.string.welcome_body),
        actions = {
            if (loading) Text(stringResource(R.string.loading), color = MaterialTheme.colorScheme.onSurfaceVariant)
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
        if (check.isUefi && state.source!!.secureBoot) Note(stringResource(R.string.secure_boot), isError = true)
    }
}

// ---- progress, done, failed ----------------------------------------------------------------

private val TASK_TITLES = mapOf(
    Task.INSTALL to R.string.installing_title,
    Task.UPDATE to R.string.updating_title,
    Task.REMOVE to R.string.removing_title,
)

@Composable
fun InstallingScreen(state: FlowState) {
    Page(icon = Icons.Rounded.Downloading, title = stringResource(TASK_TITLES.getValue(state.task)),
        body = stringResource(R.string.installing_body)) {
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
)

@Composable
fun DoneScreen(state: FlowState, actions: Actions) {
    val (title, body) = FINISHED_TEXTS.getValue(state.task)
    Page(
        icon = Icons.Rounded.CheckCircle,
        title = stringResource(title),
        body = stringResource(body),
        actions = { Button(onClick = actions.reboot) { Text(stringResource(R.string.done_reboot)) } },
    ) {
        if (state.task != Task.REMOVE && state.source?.secureBoot == true) Note(stringResource(R.string.secure_boot), isError = true)
    }
}

// ---- an installed system: update from a USB drive -------------------------------------------

@Composable
fun UpdateHomeScreen(state: FlowState, busy: Boolean, actions: Actions) {
    val running = state.source!!.installed!!
    Page(
        icon = Icons.Rounded.SystemUpdate,
        title = stringResource(R.string.update_title),
        body = stringResource(R.string.update_body),
        actions = { MainButton(stringResource(R.string.update_search), busy, onClick = actions.findUpdate) },
    ) {
        ErrorNote(state.error)
        Text(stringResource(R.string.update_current, running.version), style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
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
