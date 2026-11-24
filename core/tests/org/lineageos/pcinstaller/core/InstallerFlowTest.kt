package org.lineageos.pcinstaller.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class InstallerFlowTest {
    private val gib = 1L shl 30
    private val source = SourceInfo(uefi = true, live = true, folder = "lineage", kernel = 1, initrd = 1, system = 3 * gib)
    private val ext4 = Partition("sda3", "sda", "ext4", "Linux", 200 * gib, 100 * gib, isEsp = false,
        isSource = false, blockedBy = null, allowsFolders = true, maxFile = 0)
    private val ntfs = ext4.copy(name = "sda2", fs = "ntfs", allowsFolders = false)
    private val fresh = InstallState(hasSystem = false, hasData = false, oldSystemBytes = 0)

    private fun loaded() = InstallerFlow.reduce(FlowState(), FlowEvent.Loaded(source, listOf(ext4, ntfs)))

    @Test
    fun startsAtTheWelcomeScreen() {
        assertEquals(Screen.WELCOME, FlowState().screen)
        assertEquals(Screen.LOADING, InstallerFlow.reduce(FlowState(), FlowEvent.Start).screen)
    }

    @Test
    fun aLiveUefiSessionGoesOnToThePartitions() {
        assertEquals(Screen.PARTITIONS, loaded().screen)
    }

    @Test
    fun aLegacyBootOrAnInstalledSystemStops() {
        val bios = InstallerFlow.reduce(FlowState(), FlowEvent.Loaded(source.copy(uefi = false), listOf(ext4)))
        assertEquals(Screen.FAILED, bios.screen)
        assertEquals(InstallError.NOT_UEFI, bios.error)
        val installed = InstallerFlow.reduce(FlowState(), FlowEvent.Loaded(source.copy(live = false), listOf(ext4)))
        assertEquals(InstallError.SOURCE_MISSING, installed.error)
    }

    @Test
    fun choosingALinuxPartitionSuggestsAnImageAndADataFolder() {
        val s = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ext4, fresh))
        assertEquals(Screen.FORMS, s.screen)
        assertEquals(SystemForm.IMAGE, s.system)
        assertEquals(DataForm.FOLDER, s.data)
    }

    @Test
    fun choosingAWindowsPartitionUsesImagesAndCannotPickFolders() {
        var s = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ntfs, fresh))
        assertEquals(DataForm.IMAGE, s.data)
        assertEquals(16 * gib, s.dataSize)
        s = InstallerFlow.reduce(s, FlowEvent.SystemChosen(SystemForm.FOLDER))
        s = InstallerFlow.reduce(s, FlowEvent.DataChosen(DataForm.FOLDER))
        assertEquals(SystemForm.IMAGE, s.system)
        assertEquals(DataForm.IMAGE, s.data)
    }

    @Test
    fun anUpdateKeepsTheData() {
        val old = InstallState(hasSystem = true, hasData = true, oldSystemBytes = 0)
        val s = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ext4, old))
        assertEquals(DataForm.KEEP, s.data)
        assertEquals(DataForm.KEEP, InstallerFlow.reduce(s, FlowEvent.DataChosen(DataForm.IMAGE)).data)
    }

    @Test
    fun theDataSizeIsClampedAndRoundedToWholeGib() {
        var s = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ntfs, fresh))
        s = InstallerFlow.reduce(s, FlowEvent.DataSizeChosen(1))
        assertEquals(InstallRules.MIN_DATA, s.dataSize)
        s = InstallerFlow.reduce(s, FlowEvent.DataSizeChosen(10 * gib + 123))
        assertEquals(10 * gib, s.dataSize)
    }

    @Test
    fun aGoodCheckLeadsToTheSummaryAndABadOneStays() {
        val forms = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ext4, fresh))
        val summary = InstallerFlow.reduce(forms, FlowEvent.Checked(PlanCheck(false, 5, 9, "sda1")))
        assertEquals(Screen.SUMMARY, summary.screen)
        val refused = InstallerFlow.reduce(forms, FlowEvent.CheckFailed(InstallError.NO_ESP))
        assertEquals(Screen.FORMS, refused.screen)
        assertEquals(InstallError.NO_ESP, refused.error)
        assertNull(InstallerFlow.reduce(refused, FlowEvent.SystemChosen(SystemForm.FOLDER)).error)
    }

    @Test
    fun theInstallReportsProgressThenFinishes() {
        val forms = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ext4, fresh))
        var s = InstallerFlow.reduce(forms, FlowEvent.Checked(PlanCheck(false, 5, 9, "sda1")))
        s = InstallerFlow.reduce(s, FlowEvent.InstallStarted)
        assertEquals(Screen.INSTALLING, s.screen)
        s = InstallerFlow.reduce(s, FlowEvent.Progressed(InstallStep.SYSTEM, 42))
        assertEquals(42, s.percent)
        assertEquals(InstallStep.SYSTEM, s.step)
        assertEquals(Screen.DONE, InstallerFlow.reduce(s, FlowEvent.Installed).screen)
        val failed = InstallerFlow.reduce(s, FlowEvent.InstallFailed(InstallError.COPY_FAILED))
        assertEquals(Screen.FAILED, failed.screen)
        assertEquals(InstallError.COPY_FAILED, failed.error)
    }

    @Test
    fun backGoesOneScreenUpButNeverLeavesAnInstall() {
        val forms = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ext4, fresh))
        assertEquals(Screen.PARTITIONS, InstallerFlow.reduce(forms, FlowEvent.Back).screen)
        val installing = InstallerFlow.reduce(forms, FlowEvent.InstallStarted)
        assertEquals(Screen.INSTALLING, InstallerFlow.reduce(installing, FlowEvent.Back).screen)
    }

    @Test
    fun thePlanCarriesTheChoices() {
        var s = InstallerFlow.reduce(loaded(), FlowEvent.PartitionChosen(ntfs, fresh))
        s = InstallerFlow.reduce(s, FlowEvent.DataSizeChosen(12 * gib))
        assertEquals(InstallPlan("sda2", "lineage", SystemForm.IMAGE, DataForm.IMAGE, 12 * gib), s.plan())
    }
}
