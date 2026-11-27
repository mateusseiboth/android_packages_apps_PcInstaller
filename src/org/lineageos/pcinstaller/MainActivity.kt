package org.lineageos.pcinstaller

import android.os.Bundle
import android.os.PowerManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider
import org.lineageos.pcinstaller.ui.InstallerApp
import org.lineageos.pcinstaller.ui.PcInstallerTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        LauncherEntry.choose(this)
        val viewModel = ViewModelProvider(this)[InstallerViewModel::class.java]
        setContent {
            PcInstallerTheme {
                InstallerApp(viewModel, onReboot = ::reboot)
            }
        }
    }

    private fun reboot() = getSystemService(PowerManager::class.java).reboot(null)
}
