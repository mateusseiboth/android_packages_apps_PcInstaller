#include "biosboot.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

/* GRUB's i386-pc boot.img and diskboot.img layout (grub-core/boot/i386/pc). */
#define BOOT_BPB_START 0x03
#define BOOT_BPB_END 0x5a
#define BOOT_KERNEL_SECTOR 0x5c
#define BOOT_DRIVE 0x64
#define BOOT_DRIVE_CHECK 0x66
#define BOOT_DISK_ID 0x1b8 /* disk id, partition table and 0x55aa follow */
#define BLOCKLIST 0x1f4    /* start (8), length (2), segment (2) */
#define BLOCKLIST_SIZE 12
#define CORE_SEGMENT 0x0820
#define SECTOR 512

/* ---- where core.img goes -------------------------------------------------------------- */

static uint64_t find_first_start(const struct part_table *t)
{
	uint64_t first = UINT64_MAX;
	for (int i = 0; i < t->count; i++)
		first = t->entries[i].start < first ? t->entries[i].start : first;
	return first;
}

static int place_in_bios_boot_partition(const struct part_table *t, uint64_t sectors, uint64_t *start)
{
	for (int i = 0; i < t->count; i++) {
		if (!t->entries[i].is_bios_boot || t->entries[i].size < sectors)
			continue;
		*start = t->entries[i].start;
		return 0;
	}
	return -1;
}

/* Sector 0 holds the MBR; on GPT, the header and entries run up to first_usable. */
static int place_in_gap(const struct part_table *t, uint64_t sectors, uint64_t *start)
{
	uint64_t gap_start = t->is_gpt ? t->first_usable : 1;
	uint64_t first = find_first_start(t);
	if (!gap_start || first == UINT64_MAX || first < gap_start || first - gap_start < sectors)
		return -1;
	*start = gap_start;
	return 0;
}

int find_core_place(const struct part_table *t, unsigned lbs, uint64_t sectors, uint64_t *start)
{
	static int (*const PLACES[])(const struct part_table *, uint64_t, uint64_t *) = {
		place_in_bios_boot_partition,
		place_in_gap,
	};
	if (lbs != SECTOR)
		return -1;
	for (size_t i = 0; i < sizeof PLACES / sizeof *PLACES; i++)
		if (!PLACES[i](t, sectors, start))
			return 0;
	return -1;
}

/* ---- patching ------------------------------------------------------------------------- */

static void put_le(uint8_t *p, uint64_t v, size_t n)
{
	for (size_t i = 0; i < n; i++, v >>= 8)
		p[i] = (uint8_t)v;
}

void build_boot_sector(const uint8_t old[512], const uint8_t boot[512], uint64_t core_lba, uint8_t out[512])
{
	memcpy(out, boot, SECTOR);
	memcpy(out + BOOT_BPB_START, old + BOOT_BPB_START, BOOT_BPB_END - BOOT_BPB_START);
	memcpy(out + BOOT_DISK_ID, old + BOOT_DISK_ID, SECTOR - BOOT_DISK_ID);
	put_le(out + BOOT_KERNEL_SECTOR, core_lba, 8);
	out[BOOT_DRIVE] = 0xff;            /* use the drive the BIOS booted */
	out[BOOT_DRIVE_CHECK] = 0x90;      /* hard disk: skip the floppy drive check */
	out[BOOT_DRIVE_CHECK + 1] = 0x90;
}

void apply_core_blocklist(uint8_t *core, size_t len, uint64_t core_lba)
{
	uint64_t sectors = (len + SECTOR - 1) / SECTOR;
	memset(core + BLOCKLIST - BLOCKLIST_SIZE, 0, BLOCKLIST_SIZE); /* the list ends after one entry */
	put_le(core + BLOCKLIST, core_lba + 1, 8);
	put_le(core + BLOCKLIST + 8, sectors - 1, 2);
	put_le(core + BLOCKLIST + 10, CORE_SEGMENT, 2);
}

/* ---- writing -------------------------------------------------------------------------- */

static uint8_t *read_whole(const char *path, size_t *len)
{
	struct stat st;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) || st.st_size < SECTOR) {
		if (fd >= 0)
			close(fd);
		return NULL;
	}
	*len = (size_t)st.st_size;
	uint8_t *buf = calloc(1, (*len + SECTOR - 1) / SECTOR * SECTOR);
	int failed = !buf || read(fd, buf, *len) != (ssize_t)*len;
	close(fd);
	if (failed)
		free(buf);
	return failed ? NULL : buf;
}

/* boot.img outside the fields build_boot_sector() fills: GRUB is already there. */
static int is_grub_boot_code(const uint8_t old[512], const uint8_t boot[512])
{
	return !memcmp(old, boot, BOOT_BPB_START) &&
	       !memcmp(old + BOOT_DRIVE_CHECK + 2, boot + BOOT_DRIVE_CHECK + 2, BOOT_DISK_ID - BOOT_DRIVE_CHECK - 2);
}

static int save_backup(const uint8_t old[512], const uint8_t boot[512], const char *backup)
{
	if (is_file(backup) || is_grub_boot_code(old, boot))
		return 0;
	int fd = open(backup, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd < 0)
		return -1;
	ssize_t n = write(fd, old, SECTOR);
	return close(fd) || n != SECTOR ? -1 : 0;
}

static int write_core_and_boot(int fd, const uint8_t *boot, uint8_t *core, size_t core_len, uint64_t core_lba,
			       const char *backup)
{
	uint8_t old[SECTOR], sector[SECTOR];
	size_t padded = (core_len + SECTOR - 1) / SECTOR * SECTOR;
	if (pread(fd, old, SECTOR, 0) != SECTOR || save_backup(old, boot, backup))
		return -1;
	apply_core_blocklist(core, core_len, core_lba);
	build_boot_sector(old, boot, core_lba, sector);
	if (pwrite(fd, core, padded, (off_t)(core_lba * SECTOR)) != (ssize_t)padded)
		return -1;
	return pwrite(fd, sector, SECTOR, 0) != SECTOR || fsync(fd) ? -1 : 0;
}

int write_bios_boot(const char *disk, const char *boot_img, const char *core_img, uint64_t core_lba,
		    const char *backup)
{
	size_t boot_len = 0, core_len = 0;
	uint8_t *boot = read_whole(boot_img, &boot_len), *core = read_whole(core_img, &core_len);
	int fd = open(disk, O_RDWR | O_CLOEXEC);
	int failed = !boot || boot_len != SECTOR || !core || fd < 0 ||
		     write_core_and_boot(fd, boot, core, core_len, core_lba, backup);
	if (fd >= 0)
		close(fd);
	free(boot);
	free(core);
	return failed ? -1 : 0;
}

/* Only the boot code: the partition table may have changed since. */
int restore_bios_boot(const char *disk, const char *backup)
{
	size_t len = 0;
	uint8_t *saved = read_whole(backup, &len), now[SECTOR];
	int fd = open(disk, O_RDWR | O_CLOEXEC);
	int failed = !saved || fd < 0 || pread(fd, now, SECTOR, 0) != SECTOR;
	if (!failed) {
		memcpy(now, saved, BOOT_DISK_ID);
		failed = pwrite(fd, now, SECTOR, 0) != SECTOR || fsync(fd);
	}
	if (fd >= 0)
		close(fd);
	free(saved);
	return failed ? -1 : 0;
}
