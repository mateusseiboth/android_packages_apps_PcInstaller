package org.lineageos.pcinstaller.core

import java.io.IOException
import org.junit.Assert.assertEquals
import org.junit.Test

/** Plays the daemon: answers each request with canned lines. */
private class ScriptedTransport(private val replies: Map<String, List<String>>) : Transport {
    val sent = mutableListOf<String>()
    private val pending = ArrayDeque<String>()

    override fun send(line: String) {
        sent += line
        pending.addAll(replies.getValue(line.substringBefore(' ').trim()))
    }

    override fun readLine(): String? = pending.removeFirstOrNull()
}

private object BrokenTransport : Transport {
    override fun send(line: String) = throw IOException("no daemon")
    override fun readLine(): String? = null
}

class InstallerClientTest {
    private val gib = 1L shl 30

    @Test
    fun readsTheLiveSource() {
        val t = ScriptedTransport(mapOf("INFO" to listOf(
            "INFO version=2 uefi=1 live=1 source=sdb1 folder=lineage kernel=10 initrd=2 system=3221225472 installed=0 " +
                "install_part= install_folder= install_system= install_version= install_build_utc=0", "OK")))
        val info = (InstallerClient(t).info() as DaemonResult.Ok).value
        assertEquals(SourceInfo(uefi = true, live = true, folder = "lineage", kernel = 10, initrd = 2,
            system = 3 * gib), info)
        assertEquals(null, info.installed)
    }

    @Test
    fun readsWhichInstallIsRunning() {
        val t = ScriptedTransport(mapOf("INFO" to listOf(
            "INFO version=2 uefi=0 live=0 source= folder= kernel=0 initrd=0 system=0 installed=1 install_part=sda3 " +
                "install_folder=lineage install_system=folder install_version=24.0-old install_build_utc=100", "OK")))
        val info = (InstallerClient(t).info() as DaemonResult.Ok).value
        assertEquals(RunningInstall("sda3", "lineage", SystemForm.FOLDER, "24.0-old"), info.installed)
        assertEquals(false, info.secureBoot)
    }

    @Test
    fun readsWhetherSecureBootIsOn() {
        val t = ScriptedTransport(mapOf("INFO" to listOf("INFO version=2 uefi=1 secure_boot=1 live=1 folder=lineage", "OK")))
        assertEquals(true, (InstallerClient(t).info() as DaemonResult.Ok).value.secureBoot)
    }

    @Test
    fun readsWhetherShimTrustsTheKey() {
        val t = ScriptedTransport(mapOf("INFO" to listOf(
            "INFO version=3 uefi=1 secure_boot=1 mok_enrolled=0 mok_queued=1 live=1 folder=lineage", "OK")))
        val info = (InstallerClient(t).info() as DaemonResult.Ok).value
        assertEquals(false, info.mokEnrolled)
        assertEquals(true, info.mokQueued)
    }

    @Test
    fun queuesTheKeyAndReadsThePassword() {
        val t = ScriptedTransport(mapOf("ENROLL_KEY" to listOf("MOK password=00000042", "OK")))
        assertEquals(DaemonResult.Ok("00000042"), InstallerClient(t).enrollKey())
        assertEquals("ENROLL_KEY\n", t.sent.single())
        val failed = ScriptedTransport(mapOf("ENROLL_KEY" to listOf("ERR code=efi_failed")))
        assertEquals(DaemonResult.Failed(InstallError.EFI_FAILED), InstallerClient(failed).enrollKey())
    }

    @Test
    fun anUpdateSaysWhetherShimTrustsItsKey() {
        val t = ScriptedTransport(mapOf("FIND_UPDATE" to listOf(
            "UPDATE source=sdb1 version=24.0-new build_utc=200 current_version=24.0-old current_utc=100 newer=1 " +
                "mok_enrolled=1 mok_queued=0", "OK")))
        val update = (InstallerClient(t).findUpdate() as DaemonResult.Ok).value
        assertEquals(true, update.mokEnrolled)
        assertEquals(false, update.mokQueued)
    }

    @Test
    fun findsAnUpdateOnAUsbDrive() {
        val t = ScriptedTransport(mapOf("FIND_UPDATE" to listOf(
            "UPDATE source=sdb1 version=24.0-new build_utc=200 current_version=24.0-old current_utc=100 newer=1", "OK")))
        assertEquals(DaemonResult.Ok(UpdateInfo("sdb1", "24.0-new", "24.0-old", isNewer = true)),
            InstallerClient(t).findUpdate())
        val none = ScriptedTransport(mapOf("FIND_UPDATE" to listOf("ERR code=no_update_source")))
        assertEquals(DaemonResult.Failed(InstallError.NO_UPDATE_SOURCE), InstallerClient(none).findUpdate())
    }

    @Test
    fun uninstallsWithProgress() {
        val t = ScriptedTransport(mapOf("UNINSTALL" to listOf("PROGRESS step=remove percent=50", "OK")))
        val seen = mutableListOf<Pair<InstallStep, Int>>()
        assertEquals(DaemonResult.Ok(Unit), InstallerClient(t).uninstall("sda3", "lineage") { s, p -> seen += s to p })
        assertEquals(listOf(InstallStep.REMOVE to 50), seen)
        assertEquals("UNINSTALL part=sda3 folder=lineage\n", t.sent.single())
    }

    @Test
    fun listsPartitions() {
        val t = ScriptedTransport(mapOf("LIST" to listOf(
            "PART name=sda3 disk=sda fs=ntfs label=Windows size=500 free=200 esp=0 source=0 usable=1 reason= " +
                "folders=0 max_file=0",
            "PART name=sdb1 disk=sdb fs=vfat label=LIVE size=8 free=1 esp=0 source=1 usable=0 " +
                "reason=source_partition folders=0 max_file=4294967295",
            "OK")))
        val parts = (InstallerClient(t).partitions() as DaemonResult.Ok).value
        assertEquals(2, parts.size)
        assertEquals(Partition("sda3", "sda", "ntfs", "Windows", 500, 200, isEsp = false, isSource = false,
            blockedBy = null, allowsFolders = false, maxFile = 0), parts[0])
        assertEquals(InstallError.SOURCE_PARTITION, parts[1].blockedBy)
        assertEquals(4294967295L, parts[1].maxFile)
    }

    @Test
    fun sendsThePlanAndReadsTheCheck() {
        val t = ScriptedTransport(mapOf("CHECK" to listOf("PLAN mode=update need=5 free=9 boot=uefi esp=sda1", "OK")))
        val plan = InstallPlan("sda3", "lineage", SystemForm.FOLDER, DataForm.KEEP, 0)
        val check = (InstallerClient(t).check(plan) as DaemonResult.Ok).value
        assertEquals(PlanCheck(isUpdate = true, need = 5, free = 9, isUefi = true), check)
        assertEquals("CHECK part=sda3 folder=lineage system=folder data=keep data_size=0\n", t.sent.single())
    }

    @Test
    fun reportsProgressUntilTheEnd() {
        val t = ScriptedTransport(mapOf("INSTALL" to listOf(
            "PROGRESS step=prepare percent=0", "PROGRESS step=system percent=40", "PROGRESS step=finish percent=100",
            "OK")))
        val seen = mutableListOf<Pair<InstallStep, Int>>()
        val plan = InstallPlan("sda3", "lineage", SystemForm.IMAGE, DataForm.IMAGE, 8 * gib)
        val result = InstallerClient(t).install(plan) { step, percent -> seen += step to percent }
        assertEquals(DaemonResult.Ok(Unit), result)
        assertEquals(listOf(InstallStep.PREPARE to 0, InstallStep.SYSTEM to 40, InstallStep.FINISH to 100), seen)
    }

    @Test
    fun turnsErrorCodesIntoErrors() {
        val t = ScriptedTransport(mapOf("INSPECT" to listOf("ERR code=mount_failed")))
        assertEquals(DaemonResult.Failed(InstallError.MOUNT_FAILED), InstallerClient(t).inspect("sda3", "lineage"))
        assertEquals(InstallError.UNKNOWN, InstallError.fromCode("something_new"))
        assertEquals(InstallError.NO_BOOT_ROOM, InstallError.fromCode("no_boot_room"))
        assertEquals(InstallError.RAID, InstallError.fromCode("raid"))
    }

    @Test
    fun readsAnExistingInstall() {
        val t = ScriptedTransport(mapOf("INSPECT" to listOf("STATE system=1 data=1 old=7", "OK")))
        assertEquals(DaemonResult.Ok(InstallState(hasSystem = true, hasData = true, oldSystemBytes = 7)),
            InstallerClient(t).inspect("sda3", "lineage"))
    }

    @Test
    fun aMissingDaemonIsAnError() {
        assertEquals(DaemonResult.Failed(InstallError.DAEMON_UNREACHABLE), InstallerClient(BrokenTransport).info())
    }
}
