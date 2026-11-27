package org.lineageos.pcinstaller

import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import java.io.File

/**
 * One launcher entry for the app: "Install LineageOS" in the live session, "System updates" on an
 * installed system, where the same app updates it; there it is also in Settings > System. The choice is
 * per user, made at each user's boot.
 */
object LauncherEntry {
    /** Alias name to whether it is on in the live session. */
    private val ALIASES = mapOf("InstallEntry" to true, "UpdatesEntry" to false, "SettingsEntry" to false)

    fun choose(context: Context) {
        val isLive = isLiveSession()
        ALIASES.forEach { (alias, isForLive) -> setEnabled(context, alias, isForLive == isLive) }
    }

    /** pc-init keeps the user data in RAM for pc.live=1, the live ISO's boot entries. */
    fun isLiveSession(): Boolean =
        runCatching { File("/proc/cmdline").readText() }
            .getOrDefault("")
            .split(' ', '\n')
            .contains("pc.live=1")

    private fun setEnabled(context: Context, alias: String, isEnabled: Boolean) {
        val component = ComponentName(context, "${context.packageName}.$alias")
        val state =
            if (isEnabled) PackageManager.COMPONENT_ENABLED_STATE_ENABLED
            else PackageManager.COMPONENT_ENABLED_STATE_DISABLED
        if (context.packageManager.getComponentEnabledSetting(component) == state) return
        context.packageManager.setComponentEnabledSetting(component, state, PackageManager.DONT_KILL_APP)
    }

    class OnBoot : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == Intent.ACTION_BOOT_COMPLETED) choose(context)
        }
    }
}
