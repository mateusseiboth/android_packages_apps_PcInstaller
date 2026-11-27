package org.lineageos.pcinstaller

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
        startUpdates(viewModel, isLive)
        setContent {
            PcInstallerTheme {
                InstallerApp(viewModel, isLive, onReboot = ::reboot)
            }
        }
    }

    /** An installed system opens straight on its updates; the live session waits on the welcome page. */
    private fun startUpdates(viewModel: InstallerViewModel, isLive: Boolean) {
        if (isLive || viewModel.state.value.screen != Screen.WELCOME) return
        viewModel.start()
    }

    private fun reboot() = getSystemService(PowerManager::class.java).reboot(null)
}
