package org.lineageos.pcinstaller.core

/** What the live session can install: sizes in bytes of what gets copied. */
data class SourceInfo(
    val uefi: Boolean,
    val live: Boolean,
    val folder: String,
    val kernel: Long,
    val initrd: Long,
    val system: Long,
    /** On an installed system (not live): which install is running. */
    val installed: RunningInstall? = null,
    /** The firmware only starts signed loaders (shim, then our GRUB signed with the builder's key). */
    val secureBoot: Boolean = false,
    /** shim already trusts the live source's key (MokListRT or db). */
    val mokEnrolled: Boolean = false,
    /** MokManager will ask to enroll the key on the next start. */
    val mokQueued: Boolean = false,
) {
    val copyBytes: Long get() = kernel + initrd + system
}

data class RunningInstall(val partition: String, val folder: String, val system: SystemForm, val version: String)

/** A live USB drive plugged into an installed system. */
data class UpdateInfo(
    val source: String,
    val version: String,
    val currentVersion: String,
    val isNewer: Boolean,
    /** The same as SourceInfo's, for the key on this USB drive. */
    val mokEnrolled: Boolean = false,
    val mokQueued: Boolean = false,
)

data class Partition(
    val name: String,
    val disk: String,
    val fs: String,
    val label: String,
    val size: Long,
    val free: Long,
    val isEsp: Boolean,
    val isSource: Boolean,
    /** Why the daemon refuses this partition outright, or null. */
    val blockedBy: InstallError?,
    /** Extracted system and userdata/ folders need a Linux file system. */
    val allowsFolders: Boolean,
    /** Largest file the file system holds (FAT32), or 0 for no limit. */
    val maxFile: Long,
)

/** What <partition>/<folder> already holds. */
data class InstallState(val hasSystem: Boolean, val hasData: Boolean, val oldSystemBytes: Long)

data class PlanCheck(val isUpdate: Boolean, val need: Long, val free: Long, val isUefi: Boolean)

data class InstallPlan(
    val partition: String,
    val folder: String,
    val system: SystemForm,
    val data: DataForm,
    val dataSize: Long,
)

enum class SystemForm(val wire: String) {
    IMAGE("image"), FOLDER("folder");

    companion object {
        fun fromWire(wire: String?): SystemForm = entries.firstOrNull { it.wire == wire } ?: IMAGE
    }
}

enum class DataForm(val wire: String) { IMAGE("image"), FOLDER("folder"), KEEP("keep") }

enum class InstallStep(val wire: String) {
    PREPARE("prepare"), KERNEL("kernel"), SYSTEM("system"), DATA("data"), BOOT("boot"), REMOVE("remove"),
    FINISH("finish"),
    /** The app's own step: downloading an update or patch file. */
    DOWNLOAD("download"),
    UNKNOWN("");

    companion object {
        fun fromWire(wire: String?): InstallStep = entries.firstOrNull { it.wire == wire } ?: UNKNOWN
    }
}

/** What a package file is: a whole new system, or a patch laid over this one. */
enum class PackageType(val wire: String) {
    UPDATE("update"), PATCH("patch"), UNKNOWN("");

    companion object {
        fun fromWire(wire: String?): PackageType = entries.firstOrNull { it.wire == wire } ?: UNKNOWN
    }
}

/** What PACKAGE_INFO read from an update or patch file, next to the running system's build and flavor. */
data class PackageInfo(
    val type: PackageType,
    val version: String,
    /** Patches only: short id and one line in English. */
    val name: String,
    val description: String,
    /** Updates only: gapps, microg or none. */
    val flavor: String,
    /** ro.system.build.date.utc: of the new system (update), or of the only system it applies to (patch). */
    val build: String,
    val currentBuild: String,
    val currentFlavor: String,
    val hasKernel: Boolean,
    val hasInitrd: Boolean,
    val hasSystem: Boolean,
    val hasOverlay: Boolean,
    val size: Long,
) {
    /** Why this file does not fit the running system, or null; pc-installd checks it again on APPLY_PACKAGE. */
    fun blockedBy(): InstallError? = MISMATCHES.getValue(type)(this)
}

/** Unknown values (an empty field) never block: the daemon has the last word. */
private fun isDifferent(a: String, b: String): Boolean = a.isNotEmpty() && b.isNotEmpty() && a != b

private val MISMATCHES: Map<PackageType, (PackageInfo) -> InstallError?> = mapOf(
    PackageType.UPDATE to { p -> InstallError.WRONG_FLAVOR.takeIf { isDifferent(p.flavor, p.currentFlavor) } },
    PackageType.PATCH to { p -> InstallError.WRONG_BASE.takeIf { isDifferent(p.build, p.currentBuild) } },
    PackageType.UNKNOWN to { InstallError.BAD_PACKAGE },
)

/** A patch applied to the running system (PATCHES). */
data class Patch(
    val name: String,
    val version: String,
    val description: String,
    val replacesKernel: Boolean,
    val replacesInitrd: Boolean,
)

/** The daemon's error codes (README, "Errors"), plus the client's own. */
enum class InstallError(val code: String) {
    BAD_REQUEST("bad_request"),
    BAD_FOLDER("bad_folder"),
    SOURCE_PARTITION("source_partition"),
    FS_UNSUPPORTED("fs_unsupported"),
    FS_IMAGES_ONLY("fs_images_only"),
    FILE_TOO_LARGE("file_too_large"),
    BITLOCKER("bitlocker"),
    ENCRYPTED("encrypted"),
    LVM("lvm"),
    RAID("raid"),
    NO_SPACE("no_space"),
    DATA_TOO_SMALL("data_too_small"),
    DATA_EXISTS("data_exists"),
    NO_DATA_TO_KEEP("no_data_to_keep"),
    NO_ESP("no_esp"),
    NO_BOOT_ROOM("no_boot_room"),
    SOURCE_MISSING("source_missing"),
    UNKNOWN_PARTITION("unknown_partition"),
    MOUNT_FAILED("mount_failed"),
    COPY_FAILED("copy_failed"),
    DATA_FAILED("data_failed"),
    ESP_FAILED("esp_failed"),
    EFI_FAILED("efi_failed"),
    BOOT_FAILED("boot_failed"),
    MENU_FAILED("menu_failed"),
    NOT_INSTALLED("not_installed"),
    RUNNING_SYSTEM("running_system"),
    NO_UPDATE_SOURCE("no_update_source"),
    IO_ERROR("io_error"),
    BAD_PACKAGE("bad_package"),
    BAD_SIGNATURE("bad_signature"),
    NO_CERTIFICATE("no_certificate"),
    WRONG_FLAVOR("wrong_flavor"),
    WRONG_BASE("wrong_base"),
    NO_ROOM("no_room"),
    IO("io"),
    DAEMON_UNREACHABLE("daemon_unreachable"),
    /** The chosen update or patch file could not be opened. */
    FILE_UNREADABLE("file_unreadable"),
    /** The online update list could not be fetched, or could not be read. */
    NETWORK("network"),
    CATALOG_UNREADABLE("catalog_unreadable"),
    /** A download stopped, or its size or sha256 is not the list's. */
    DOWNLOAD_FAILED("download_failed"),
    DOWNLOAD_DAMAGED("download_damaged"),
    UNKNOWN("unknown");

    companion object {
        fun fromCode(code: String?): InstallError = entries.firstOrNull { it.code == code } ?: UNKNOWN
    }
}

sealed interface DaemonResult<out T> {
    data class Ok<T>(val value: T) : DaemonResult<T>
    data class Failed(val error: InstallError) : DaemonResult<Nothing>
}

inline fun <T, R> DaemonResult<T>.fold(onOk: (T) -> R, onFailed: (InstallError) -> R): R = when (this) {
    is DaemonResult.Ok -> onOk(value)
    is DaemonResult.Failed -> onFailed(error)
}
