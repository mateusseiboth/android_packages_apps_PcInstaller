package org.lineageos.pcinstaller.core

/**
 * pc-installd's line protocol: an upper-case WORD and key=value fields separated by single spaces.
 * Values are percent-encoded UTF-8; bytes outside [A-Za-z0-9._/:+,-] become %XX.
 */
data class Message(val word: String, val fields: Map<String, String>) {
    operator fun get(key: String): String? = fields[key]

    fun long(key: String): Long = fields[key]?.toLongOrNull() ?: 0L

    fun flag(key: String): Boolean = fields[key] == "1"
}

object LineCodec {
    private const val SAFE = "._/:+,-"
    private val WORD = Regex("[A-Z_]+")

    fun format(word: String, vararg fields: Pair<String, Any>): String =
        fields.joinToString(separator = "", prefix = word, postfix = "\n") { (k, v) -> " $k=${encode(v.toString())}" }

    fun parse(line: String): Message? {
        val words = line.trimEnd('\n', '\r').split(' ')
        val word = words.first().takeIf { WORD.matches(it) } ?: return null
        val fields = words.drop(1).map { parseField(it) ?: return null }
        return Message(word, fields.toMap())
    }

    private fun parseField(token: String): Pair<String, String>? {
        val key = token.substringBefore('=', missingDelimiterValue = "")
        if (key.isEmpty()) return null
        return decode(token.substringAfter('='))?.let { key to it }
    }

    private fun isSafe(b: Int): Boolean =
        b in 'a'.code..'z'.code || b in 'A'.code..'Z'.code || b in '0'.code..'9'.code || SAFE.indexOf(b.toChar()) >= 0

    private fun encode(value: String): String =
        value.toByteArray(Charsets.UTF_8).joinToString("") { byte ->
            val b = byte.toInt() and 0xff
            b.toChar().toString().takeIf { isSafe(b) } ?: "%%%02X".format(b)
        }

    private fun decode(value: String): String? {
        val out = java.io.ByteArrayOutputStream()
        var i = 0
        while (i < value.length) {
            val c = value[i]
            if (c != '%') {
                out.write(c.toString().toByteArray(Charsets.UTF_8))
                i++
                continue
            }
            val hex = value.substring(i + 1, minOf(i + 3, value.length)).toIntOrNull(16)
            if (hex == null || i + 3 > value.length) return null
            out.write(hex)
            i += 3
        }
        return out.toString(Charsets.UTF_8.name())
    }
}
