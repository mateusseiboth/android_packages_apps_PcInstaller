package org.lineageos.pcinstaller

import android.os.SystemProperties
import java.io.File
import org.lineageos.pcinstaller.core.RunningBuild
import org.lineageos.pcinstaller.core.Updates

/** The running system's build and edition, read the way pc-installd reads them (build.prop, /system folders). */
object RunningSystem {
    private const val SYSTEM_ROOT = "/system"

    fun read(): RunningBuild = RunningBuild(
        dateUtc = SystemProperties.getLong("ro.build.date.utc", 0),
        build = SystemProperties.get("ro.system.build.date.utc"),
        flavor = Updates.findEdition { File(SYSTEM_ROOT, it).isDirectory },
    )
}
