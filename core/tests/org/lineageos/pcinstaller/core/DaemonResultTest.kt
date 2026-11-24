package org.lineageos.pcinstaller.core

import org.junit.Assert.assertEquals
import org.junit.Test

class DaemonResultTest {
    @Test
    fun foldPicksTheBranch() {
        val ok: DaemonResult<Int> = DaemonResult.Ok(2)
        val failed: DaemonResult<Int> = DaemonResult.Failed(InstallError.NO_SPACE)
        assertEquals("4", ok.fold({ (it * 2).toString() }, { it.code }))
        assertEquals("no_space", failed.fold({ it.toString() }, { it.code }))
    }
}
