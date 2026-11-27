package org.lineageos.pcinstaller

import android.app.ActivityManager
import android.os.Bundle
import android.os.PowerManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider
import org.lineageos.pcinstaller.core.Screen
import org.lineageos.pcinstaller.ui.InstallerApp
import org.lineageos.pcinstaller.ui.PcInstallerTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        LauncherEntry.choose(this)
        val isLive = LauncherEntry.isLiveSession()
        val viewModel = ViewModelProvider(this)[InstallerViewModel::class.java]
        if (!isLive) nameAsUpdates()
        startUpdates(viewModel, isLive)
        setContent {
            PcInstallerTheme {
                InstallerApp(viewModel, isLive, onReboot = ::reboot)
            }
        }
    }

    /** The window and the recent apps say "System updates", not the app's install name. */
    private fun nameAsUpdates() {
        val name = getString(R.string.updates_name)
        title = name
        setTaskDescription(ActivityManager.TaskDescription.Builder().setLabel(name).build())
    }

    /** An installed system opens straight on its updates; the live session waits on the welcome page. */
    private fun startUpdates(viewModel: InstallerViewModel, isLive: Boolean) {
        if (isLive || viewModel.state.value.screen != Screen.WELCOME) return
        viewModel.start()
    }

    private fun reboot() = getSystemService(PowerManager::class.java).reboot(null)
}
