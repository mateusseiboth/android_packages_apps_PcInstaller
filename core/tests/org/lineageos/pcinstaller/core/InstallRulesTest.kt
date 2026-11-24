package org.lineageos.pcinstaller.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class InstallRulesTest {
    private val gib = 1L shl 30
    private val source = SourceInfo(uefi = true, live = true, folder = "lineage", kernel = 0, initrd = 0, system = 3 * gib)

    private fun part(fs: String, free: Long, folders: Boolean = true, maxFile: Long = 0) =
        Partition("sda3", "sda", fs, "", free * 2, free, isEsp = false, isSource = false, blockedBy = null,
            allowsFolders = folders, maxFile = maxFile)

    private val fresh = InstallState(hasSystem = false, hasData = false, oldSystemBytes = 0)

    @Test
    fun linuxFileSystemsOfferEveryForm() {
        val p = part("ext4", 100 * gib)
        assertEquals(setOf(SystemForm.IMAGE, SystemForm.FOLDER), InstallRules.systemForms(p))
        assertEquals(setOf(DataForm.IMAGE, DataForm.FOLDER), InstallRules.dataForms(p, fresh))
    }

    @Test
    fun windowsFileSystemsOfferOnlyImages() {
        val p = part("ntfs", 100 * gib, folders = false)
        assertEquals(setOf(SystemForm.IMAGE), InstallRules.systemForms(p))
        assertEquals(setOf(DataForm.IMAGE), InstallRules.dataForms(p, fresh))
    }

    @Test
    fun anExistingInstallKeepsItsData() {
        val old = InstallState(hasSystem = true, hasData = true, oldSystemBytes = 0)
        assertEquals(setOf(DataForm.KEEP), InstallRules.dataForms(part("ext4", 100 * gib), old))
    }

    @Test
    fun theDataImageFitsTheFreeSpaceAndTheFileLimit() {
        val need = 3 * gib + InstallRules.MARGIN
        assertEquals(20 * gib, InstallRules.maxDataSize(part("ext4", need + 20 * gib + 5), source, fresh))
        val fat = part("vfat", 100 * gib, folders = false, maxFile = 4 * gib - 1)
        assertEquals(3 * gib, InstallRules.maxDataSize(fat, source, fresh))
    }

    @Test
    fun theDefaultDataSizeStaysWithinBounds() {
        assertEquals(16 * gib, InstallRules.defaultDataSize(100 * gib))
        assertEquals(5 * gib, InstallRules.defaultDataSize(5 * gib))
    }

    @Test
    fun blocksPartitionsThatCannotHoldTheSystem() {
        assertNull(InstallRules.blockReason(part("ext4", 100 * gib), source))
        assertEquals(InstallError.NO_SPACE, InstallRules.blockReason(part("ext4", 3 * gib), source))
        // Images only: there must also be room for the smallest data image.
        assertEquals(InstallError.NO_SPACE,
            InstallRules.blockReason(part("ntfs", 3 * gib + InstallRules.MARGIN + gib, folders = false), source))
        val bitlocker = part("bitlocker", 100 * gib).copy(blockedBy = InstallError.BITLOCKER)
        assertEquals(InstallError.BITLOCKER, InstallRules.blockReason(bitlocker, source))
    }
}
