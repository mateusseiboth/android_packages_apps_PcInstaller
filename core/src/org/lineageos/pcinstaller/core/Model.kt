package org.lineageos.pcinstaller.core

/** What the live session can install: sizes in bytes of what gets copied. */
data class SourceInfo(
    val uefi: Boolean,
    val live: Boolean,
    val folder: String,
    val kernel: Long,
    val initrd: Long,
    val system: Long,
) {
    val copyBytes: Long get() = kernel + initrd + system
}

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

data class PlanCheck(val isUpdate: Boolean, val need: Long, val free: Long, val esp: String)

data class InstallPlan(
    val partition: String,
    val folder: String,
    val system: SystemForm,
    val data: DataForm,
    val dataSize: Long,
)

enum class SystemForm(val wire: String) { IMAGE("image"), FOLDER("folder") }

enum class DataForm(val wire: String) { IMAGE("image"), FOLDER("folder"), KEEP("keep") }

enum class InstallStep(val wire: String) {
    PREPARE("prepare"), KERNEL("kernel"), SYSTEM("system"), DATA("data"), BOOT("boot"), FINISH("finish"),
    UNKNOWN("");

    companion object {
        fun fromWire(wire: String?): InstallStep = entries.firstOrNull { it.wire == wire } ?: UNKNOWN
    }
}

/** The daemon's error codes (installd README, "Errors"), plus the client's own. */
enum class InstallError(val code: String) {
    BAD_REQUEST("bad_request"),
    BAD_FOLDER("bad_folder"),
    SOURCE_PARTITION("source_partition"),
    FS_UNSUPPORTED("fs_unsupported"),
    FS_IMAGES_ONLY("fs_images_only"),
    FILE_TOO_LARGE("file_too_large"),
    BITLOCKER("bitlocker"),
    ENCRYPTED("encrypted"),
    NO_SPACE("no_space"),
    DATA_TOO_SMALL("data_too_small"),
    DATA_EXISTS("data_exists"),
    NO_DATA_TO_KEEP("no_data_to_keep"),
    NOT_UEFI("not_uefi"),
    NO_ESP("no_esp"),
    SOURCE_MISSING("source_missing"),
    UNKNOWN_PARTITION("unknown_partition"),
    MOUNT_FAILED("mount_failed"),
    COPY_FAILED("copy_failed"),
    DATA_FAILED("data_failed"),
    ESP_FAILED("esp_failed"),
    EFI_FAILED("efi_failed"),
    IO_ERROR("io_error"),
    DAEMON_UNREACHABLE("daemon_unreachable"),
    UNKNOWN("unknown");

    companion object {
        fun fromCode(code: String?): InstallError = entries.firstOrNull { it.code == code } ?: UNKNOWN
    }
}

sealed interface DaemonResult<out T> {
    data class Ok<T>(val value: T) : DaemonResult<T>
    data class Failed(val error: InstallError) : DaemonResult<Nothing>
}
