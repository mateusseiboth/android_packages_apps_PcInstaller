package org.lineageos.pcinstaller.core

/** The running system as the update list names it, read on the device (build.prop and the installed apps). */
data class RunningBuild(
    /** ro.build.date.utc in seconds: when the system was built, shown as a date. */
    val dateUtc: Long,
    /** ro.system.build.date.utc as a decimal string: what updates and patches are compared with. */
    val build: String,
    /** gapps, microg or none. */
    val flavor: String,
)

/** One file of the online update list (updates.json): an update of one edition, or a patch. */
data class CatalogEntry(
    val name: String,
    val version: String,
    val build: String,
    val description: String,
    val url: String,
    val size: Long,
    val sha256: String,
)

/** updates.json: the latest update of each edition and the patches published for each build. */
data class Catalog(val updates: Map<String, CatalogEntry>, val patches: List<CatalogEntry>)

/** What the online list has for this system. */
data class Offer(val update: CatalogEntry?, val patches: List<CatalogEntry>) {
    val isEmpty: Boolean get() = update == null && patches.isEmpty()
}

object Updates {
    /** The marks pc-installd reads for the edition (installd/src/source.c, FLAVOR_MARKS), relative to /system. */
    private val EDITION_MARKS = listOf(
        "product/priv-app/Phonesky" to "gapps",
        "system_ext/priv-app/Phonesky" to "gapps",
        "priv-app/Phonesky" to "gapps",
        "product/priv-app/GmsCore" to "microg",
        "system_ext/priv-app/GmsCore" to "microg",
        "priv-app/GmsCore" to "microg",
    )

    /** microG's FakeStore also answers to the Play Store's package, so the edition is told by folders. */
    fun findEdition(isDir: (String) -> Boolean): String = EDITION_MARKS.firstOrNull { isDir(it.first) }?.second ?: "none"

    /** An update of the running edition with a newer build and a file to download; patches for this build not applied yet. */
    fun findOffer(catalog: Catalog, running: RunningBuild, applied: List<Patch>): Offer = Offer(
        update = catalog.updates[running.flavor]?.takeIf { it.url.isNotEmpty() && isNewer(it.build, running.build) },
        patches = catalog.patches.filter { it.url.isNotEmpty() && it.build == running.build && !isApplied(it, applied) },
    )

    /** Builds are decimal seconds; an unreadable one is never newer. */
    fun isNewer(build: String, current: String): Boolean {
        val candidate = build.toBigIntegerOrNull() ?: return false
        val running = current.toBigIntegerOrNull() ?: return true
        return candidate > running
    }

    private fun isApplied(entry: CatalogEntry, applied: List<Patch>): Boolean =
        applied.any { it.name == entry.name && it.version == entry.version }
}
