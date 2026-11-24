package org.lineageos.pcinstaller.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class LineCodecTest {
    @Test
    fun formatsWordAndEncodedFields() {
        val line = LineCodec.format("CHECK", "part" to "sda3", "label" to "Meus Dados", "size" to 8L)
        assertEquals("CHECK part=sda3 label=Meus%20Dados size=8\n", line)
    }

    @Test
    fun parsesWhatTheDaemonSends() {
        val m = LineCodec.parse("PART name=sda3 label=Disco%20C%3A reason=\n")!!
        assertEquals("PART", m.word)
        assertEquals("Disco C:", m["label"])
        assertEquals("", m["reason"])
        assertNull(m["missing"])
    }

    @Test
    fun keepsUtf8() {
        val line = LineCodec.format("X", "label" to "Música")
        assertEquals("Música", LineCodec.parse(line)!!["label"])
    }

    @Test
    fun refusesMalformedLines() {
        assertNull(LineCodec.parse(""))
        assertNull(LineCodec.parse("lower case=1"))
        assertNull(LineCodec.parse("OK broken"))
        assertNull(LineCodec.parse("OK bad=%zz"))
    }
}
