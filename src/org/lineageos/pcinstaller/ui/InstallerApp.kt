package org.lineageos.pcinstaller.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import org.lineageos.pcinstaller.InstallerViewModel
import org.lineageos.pcinstaller.core.FlowState
import org.lineageos.pcinstaller.core.Screen

private val BACKABLE = setOf(Screen.PARTITIONS, Screen.FORMS, Screen.SUMMARY, Screen.FAILED, Screen.UPDATE_FOUND)

@Composable
private fun ScreenContent(screen: Screen, state: FlowState, busy: Boolean, actions: Actions) = when (screen) {
    Screen.WELCOME -> WelcomeScreen(actions, loading = false)
    Screen.LOADING -> WelcomeScreen(actions, loading = true)
    Screen.PARTITIONS -> PartitionsScreen(state, busy, actions)
    Screen.FORMS -> FormsScreen(state, busy, actions)
    Screen.SUMMARY -> SummaryScreen(state, busy, actions)
    Screen.INSTALLING -> InstallingScreen(state)
    Screen.DONE, Screen.REMOVED -> DoneScreen(state, actions)
    Screen.FAILED -> FailedScreen(state, actions)
    Screen.UPDATE_HOME -> UpdateHomeScreen(state, busy, actions)
    Screen.UPDATE_FOUND -> UpdateFoundScreen(state, busy, actions)
}

@Composable
fun InstallerApp(viewModel: InstallerViewModel, onReboot: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val busy by viewModel.busy.collectAsStateWithLifecycle()
    val actions = Actions(
        start = viewModel::start,
        back = viewModel::back,
        choosePartition = viewModel::choosePartition,
        chooseSystem = viewModel::chooseSystem,
        chooseData = viewModel::chooseData,
        chooseDataSize = viewModel::chooseDataSize,
        review = viewModel::review,
        install = viewModel::install,
        reboot = onReboot,
        findUpdate = viewModel::findUpdate,
        askUninstall = viewModel::askUninstall,
        cancelUninstall = viewModel::cancelUninstall,
        uninstall = viewModel::uninstall,
    )
    BackHandler(enabled = state.screen in BACKABLE) { viewModel.back() }
    Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        AnimatedContent(
            targetState = state.screen,
            transitionSpec = { fadeIn() togetherWith fadeOut() },
            label = "screen",
        ) { screen ->
            ScreenContent(screen, state, busy, actions)
        }
    }
}
