package org.lineageos.pcinstaller.ui

import android.content.Context
import android.text.format.Formatter
import org.lineageos.pcinstaller.R
import org.lineageos.pcinstaller.core.DataForm
import org.lineageos.pcinstaller.core.InstallError
import org.lineageos.pcinstaller.core.InstallStep
import org.lineageos.pcinstaller.core.SystemForm

/** User-facing texts for the core's codes. */
object Texts {
    private val ERRORS = mapOf(
        InstallError.BITLOCKER to R.string.error_bitlocker,
        InstallError.ENCRYPTED to R.string.error_encrypted,
        InstallError.FS_UNSUPPORTED to R.string.error_fs_unsupported,
        InstallError.SOURCE_PARTITION to R.string.error_source_partition,
        InstallError.NO_SPACE to R.string.error_no_space,
        InstallError.FS_IMAGES_ONLY to R.string.error_fs_images_only,
        InstallError.FILE_TOO_LARGE to R.string.error_file_too_large,
        InstallError.DATA_TOO_SMALL to R.string.error_data_too_small,
        InstallError.NOT_UEFI to R.string.error_not_uefi,
        InstallError.NO_ESP to R.string.error_no_esp,
        InstallError.SOURCE_MISSING to R.string.error_source_missing,
        InstallError.UNKNOWN_PARTITION to R.string.error_unknown_partition,
        InstallError.MOUNT_FAILED to R.string.error_mount_failed,
        InstallError.COPY_FAILED to R.string.error_copy_failed,
        InstallError.DATA_FAILED to R.string.error_data_failed,
        InstallError.ESP_FAILED to R.string.error_esp_failed,
        InstallError.EFI_FAILED to R.string.error_efi_failed,
        InstallError.IO_ERROR to R.string.error_io_error,
        InstallError.DAEMON_UNREACHABLE to R.string.error_daemon_unreachable,
    )

    private val STEPS = mapOf(
        InstallStep.PREPARE to R.string.step_prepare,
        InstallStep.KERNEL to R.string.step_kernel,
        InstallStep.SYSTEM to R.string.step_system,
        InstallStep.DATA to R.string.step_data,
        InstallStep.BOOT to R.string.step_boot,
        InstallStep.FINISH to R.string.step_finish,
    )

    private val SYSTEM_FORMS = mapOf(SystemForm.IMAGE to R.string.system_image, SystemForm.FOLDER to R.string.system_folder)

    private val DATA_FORMS = mapOf(
        DataForm.IMAGE to R.string.data_image,
        DataForm.FOLDER to R.string.data_folder,
        DataForm.KEEP to R.string.data_keep,
    )

    private val FILE_SYSTEMS = mapOf(
        "ext4" to "ext4", "f2fs" to "F2FS", "btrfs" to "Btrfs", "ntfs" to "NTFS", "exfat" to "exFAT",
        "vfat" to "FAT32", "iso9660" to "ISO 9660", "erofs" to "EROFS", "swap" to "swap",
        "bitlocker" to "BitLocker", "luks" to "LUKS", "lvm" to "LVM",
    )

    fun error(error: InstallError): Int = ERRORS[error] ?: R.string.error_unknown

    fun step(step: InstallStep): Int = STEPS[step] ?: R.string.step_finish

    fun system(form: SystemForm): Int = SYSTEM_FORMS.getValue(form)

    fun data(form: DataForm): Int = DATA_FORMS.getValue(form)

    fun fileSystem(context: Context, fs: String): String = FILE_SYSTEMS[fs] ?: context.getString(R.string.fs_unknown)

    fun bytes(context: Context, bytes: Long): String = Formatter.formatFileSize(context, bytes)
}
