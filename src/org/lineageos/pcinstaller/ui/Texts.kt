package org.lineageos.pcinstaller.ui

import android.content.Context
import android.text.format.Formatter
import java.text.DateFormat
import java.util.Date
import java.util.Locale
import org.lineageos.pcinstaller.R
import org.lineageos.pcinstaller.core.DataForm
import org.lineageos.pcinstaller.core.DownloadPhase
import org.lineageos.pcinstaller.core.DownloadProgress
import org.lineageos.pcinstaller.core.InstallError
import org.lineageos.pcinstaller.core.InstallStep
import org.lineageos.pcinstaller.core.SystemForm

/** User-facing texts for the core's codes. */
object Texts {
    private val ERRORS = mapOf(
        InstallError.BITLOCKER to R.string.error_bitlocker,
        InstallError.ENCRYPTED to R.string.error_encrypted,
        InstallError.LVM to R.string.error_lvm,
        InstallError.RAID to R.string.error_raid,
        InstallError.FS_UNSUPPORTED to R.string.error_fs_unsupported,
        InstallError.SOURCE_PARTITION to R.string.error_source_partition,
        InstallError.NO_SPACE to R.string.error_no_space,
        InstallError.FS_IMAGES_ONLY to R.string.error_fs_images_only,
        InstallError.FILE_TOO_LARGE to R.string.error_file_too_large,
        InstallError.DATA_TOO_SMALL to R.string.error_data_too_small,
        InstallError.NO_ESP to R.string.error_no_esp,
        InstallError.NO_BOOT_ROOM to R.string.error_no_boot_room,
        InstallError.SOURCE_MISSING to R.string.error_source_missing,
        InstallError.UNKNOWN_PARTITION to R.string.error_unknown_partition,
        InstallError.MOUNT_FAILED to R.string.error_mount_failed,
        InstallError.COPY_FAILED to R.string.error_copy_failed,
        InstallError.DATA_FAILED to R.string.error_data_failed,
        InstallError.ESP_FAILED to R.string.error_esp_failed,
        InstallError.EFI_FAILED to R.string.error_efi_failed,
        InstallError.IO_ERROR to R.string.error_io_error,
        InstallError.BOOT_FAILED to R.string.error_boot_failed,
        InstallError.MENU_FAILED to R.string.error_menu_failed,
        InstallError.NOT_INSTALLED to R.string.error_not_installed,
        InstallError.RUNNING_SYSTEM to R.string.error_running_system,
        InstallError.NO_UPDATE_SOURCE to R.string.error_no_update_source,
        InstallError.DAEMON_UNREACHABLE to R.string.error_daemon_unreachable,
        InstallError.BAD_PACKAGE to R.string.error_bad_package,
        InstallError.BAD_SIGNATURE to R.string.error_bad_signature,
        InstallError.NO_CERTIFICATE to R.string.error_no_certificate,
        InstallError.WRONG_FLAVOR to R.string.error_wrong_flavor,
        InstallError.WRONG_BASE to R.string.error_wrong_base,
        InstallError.NO_ROOM to R.string.error_no_room,
        InstallError.IO to R.string.error_io_error,
        InstallError.FILE_UNREADABLE to R.string.error_file_unreadable,
        InstallError.NETWORK to R.string.error_network,
        InstallError.CATALOG_UNREADABLE to R.string.error_catalog_unreadable,
        InstallError.DOWNLOAD_FAILED to R.string.error_download_failed,
        InstallError.DOWNLOAD_DAMAGED to R.string.error_download_damaged,
    )

    /** An update's flavor, as the edition the person downloaded. */
    private val EDITIONS = mapOf(
        "gapps" to R.string.edition_gapps,
        "microg" to R.string.edition_microg,
        "none" to R.string.edition_none,
    )

    /** The running system's edition inside a sentence ("Installed system: <date>, <edition>."). */
    private val INSTALLED_EDITIONS = mapOf(
        "gapps" to R.string.installed_edition_gapps,
        "microg" to R.string.installed_edition_microg,
        "none" to R.string.installed_edition_none,
    )

    private val STEPS = mapOf(
        InstallStep.PREPARE to R.string.step_prepare,
        InstallStep.KERNEL to R.string.step_kernel,
        InstallStep.SYSTEM to R.string.step_system,
        InstallStep.DATA to R.string.step_data,
        InstallStep.BOOT to R.string.step_boot,
        InstallStep.REMOVE to R.string.step_remove,
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

    fun edition(context: Context, flavor: String): String = EDITIONS[flavor]?.let(context::getString) ?: flavor

    /** A build time (seconds) as a date in the app's language, e.g. "27 de nov. de 2026". */
    fun date(utcSeconds: Long, locale: Locale): String =
        DateFormat.getDateInstance(DateFormat.MEDIUM, locale).format(Date(utcSeconds * 1000))

    /** "<date>, <edition>" for the running system; the edition alone names it when the date is unknown. */
    fun installedSystem(context: Context, utcSeconds: Long, flavor: String): String {
        val locale = context.resources.configuration.locales[0]
        val edition = INSTALLED_EDITIONS[flavor]?.let(context::getString)
        val date = date(utcSeconds, locale).takeIf { utcSeconds > 0 }
        return listOfNotNull(date, edition).joinToString(", ")
    }

    private val DOWNLOAD_PHASES = mapOf(
        DownloadPhase.CONNECTING to R.string.download_connecting,
        DownloadPhase.DOWNLOADING to R.string.step_download,
        DownloadPhase.CHECKING to R.string.download_checking,
    )

    /** Seconds left: under a minute, minutes, or hours and minutes. */
    private val TIME_LEFT: List<Pair<Long, (Context, Long) -> String>> = listOf(
        60L to { c, _ -> c.getString(R.string.time_left_under_minute) },
        3_600L to { c, s -> c.getString(R.string.time_left_minutes, (s + 59) / 60) },
        Long.MAX_VALUE to { c, s -> c.getString(R.string.time_left_hours, s / 3_600, s % 3_600 / 60) },
    )

    fun downloadPhase(phase: DownloadPhase): Int = DOWNLOAD_PHASES.getValue(phase)

    fun timeLeft(context: Context, seconds: Long): String = TIME_LEFT.first { seconds < it.first }.second(context, seconds)

    /** "235 MB of 1.9 GB · 6.1 MB/s · 4 min left", leaving out what is not known yet. */
    fun downloadDetails(context: Context, p: DownloadProgress): String = listOfNotNull(
        context.getString(R.string.download_bytes, bytes(context, p.done), bytes(context, p.total)).takeIf { p.total > 0 },
        context.getString(R.string.download_speed, bytes(context, p.bytesPerSecond)).takeIf { p.bytesPerSecond > 0 },
        p.secondsLeft?.let { timeLeft(context, it) },
    ).joinToString(" · ")

    fun bytes(context: Context, bytes: Long): String = Formatter.formatFileSize(context, bytes)
}
