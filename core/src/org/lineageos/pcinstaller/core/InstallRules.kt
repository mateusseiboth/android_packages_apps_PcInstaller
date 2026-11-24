package org.lineageos.pcinstaller.core

/** The choices a partition allows. pc-installd checks the same rules again (installd/src/plan.c). */
object InstallRules {
    const val GIB = 1L shl 30
    const val MIN_DATA = 2 * GIB
    const val MARGIN = 256L shl 20
    private const val DEFAULT_DATA = 16 * GIB

    private val SYSTEM_FORMS = mapOf(
        true to setOf(SystemForm.IMAGE, SystemForm.FOLDER),
        false to setOf(SystemForm.IMAGE),
    )
    private val NEW_DATA_FORMS = mapOf(
        true to setOf(DataForm.IMAGE, DataForm.FOLDER),
        false to setOf(DataForm.IMAGE),
    )

    private val DATA_NEED = mapOf(true to 0L, false to MIN_DATA)

    fun systemForms(p: Partition): Set<SystemForm> = SYSTEM_FORMS.getValue(p.allowsFolders)

    /** An existing install keeps its data: an update never wipes it. */
    fun dataForms(p: Partition, state: InstallState): Set<DataForm> = when {
        state.hasData -> setOf(DataForm.KEEP)
        else -> NEW_DATA_FORMS.getValue(p.allowsFolders)
    }

    fun maxDataSize(p: Partition, source: SourceInfo, state: InstallState): Long {
        val room = p.free + state.oldSystemBytes - source.copyBytes - MARGIN
        val limit = p.maxFile.takeIf { it > 0 } ?: Long.MAX_VALUE
        return floorGib(minOf(room, limit)).coerceAtLeast(0)
    }

    fun defaultDataSize(max: Long): Long = minOf(DEFAULT_DATA, max)

    fun floorGib(bytes: Long): Long = bytes / GIB * GIB

    /** Why the partition cannot be chosen, or null. File systems without folders also need a data image. */
    fun blockReason(p: Partition, source: SourceInfo): InstallError? {
        p.blockedBy?.let { return it }
        val dataNeed = DATA_NEED.getValue(p.allowsFolders)
        return InstallError.NO_SPACE.takeIf { p.free < source.copyBytes + MARGIN + dataNeed }
    }
}
