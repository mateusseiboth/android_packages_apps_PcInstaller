package org.lineageos.pcinstaller.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import org.lineageos.pcinstaller.R

private val PAGE_WIDTH = 760.dp

/** A page of the installer: step counter, icon, title, text, scrolling content and a bottom action row. */
@Composable
fun Page(
    icon: ImageVector,
    title: String,
    body: String?,
    step: Int? = null,
    actions: @Composable RowScope.() -> Unit = {},
    content: @Composable ColumnScope.() -> Unit = {},
) {
    Box(Modifier.fillMaxSize().safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = PAGE_WIDTH).fillMaxSize().padding(horizontal = 32.dp, vertical = 24.dp)) {
            Column(
                Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Header(icon, title, body, step)
                content()
            }
            Row(
                Modifier.fillMaxWidth().padding(top = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp, Alignment.End),
                verticalAlignment = Alignment.CenterVertically,
                content = actions,
            )
        }
    }
}

@Composable
private fun Header(icon: ImageVector, title: String, body: String?, step: Int?) {
    Column(Modifier.padding(top = 32.dp, bottom = 8.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Surface(shape = RoundedCornerShape(20.dp), color = MaterialTheme.colorScheme.primaryContainer) {
            Icon(icon, null, Modifier.padding(14.dp).size(36.dp), tint = MaterialTheme.colorScheme.onPrimaryContainer)
        }
        step?.let {
            Text(
                stringResource(R.string.step_of, it, STEP_COUNT),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary,
            )
        }
        Text(title, style = MaterialTheme.typography.headlineLarge)
        body?.let { Text(it, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant) }
    }
}

const val STEP_COUNT = 3

/** A selectable option with an icon; disabled options say why. */
@Composable
fun ChoiceCard(
    icon: ImageVector,
    title: String,
    body: String,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    val border = BorderStroke(
        width = mapOf(true to 2.dp, false to 1.dp).getValue(selected),
        color = mapOf(true to MaterialTheme.colorScheme.primary, false to MaterialTheme.colorScheme.outlineVariant)
            .getValue(selected),
    )
    Card(
        onClick = onClick,
        enabled = enabled,
        shape = RoundedCornerShape(20.dp),
        border = border,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(icon, null, Modifier.size(32.dp), tint = MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f).padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(title, style = MaterialTheme.typography.titleMedium)
                Text(body, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            RadioButton(selected = selected, onClick = onClick, enabled = enabled)
        }
    }
}

@Composable
fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.padding(top = 8.dp))
}

/** A short note: information, or an error when isError. */
@Composable
fun Note(text: String, isError: Boolean = false) {
    val colors = mapOf(
        false to (MaterialTheme.colorScheme.secondaryContainer to MaterialTheme.colorScheme.onSecondaryContainer),
        true to (MaterialTheme.colorScheme.errorContainer to MaterialTheme.colorScheme.onErrorContainer),
    ).getValue(isError)
    Surface(shape = RoundedCornerShape(16.dp), color = colors.first, contentColor = colors.second) {
        Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Rounded.Info, null)
            Spacer(Modifier.size(12.dp))
            Text(text, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@Composable
fun BusyIndicator() {
    CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 3.dp)
}

@Composable
fun SummaryRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 10.dp)) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, Modifier.weight(2f), style = MaterialTheme.typography.bodyLarge)
    }
}

@Composable
fun Gap() = Spacer(Modifier.height(4.dp))
