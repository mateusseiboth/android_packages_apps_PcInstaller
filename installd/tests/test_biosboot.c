#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "biosboot.h"
#include "check.h"

static uint8_t head[PARTTABLE_HEAD_BYTES];
static char disk[] = "/tmp/biosdisk-XXXXXX";

/* A 64 MiB disk image partitioned by cmd (sgdisk/sfdisk); its head goes to head. */
static int make_disk(const char *cmd_format)
{
	snprintf(disk, sizeof disk, "/tmp/biosdisk-XXXXXX");
	int fd = mkstemp(disk);
	CHECK(fd >= 0 && !ftruncate(fd, 64 << 20));
	char cmd[512];
	snprintf(cmd, sizeof cmd, cmd_format, disk);
	int failed = system(cmd);
	ssize_t n = failed ? -1 : pread(fd, head, sizeof head, 0);
	close(fd);
	return n == (ssize_t)sizeof head ? 0 : -1;
}

static int place_core(const char *cmd, uint64_t sectors, unsigned lbs, uint64_t *start)
{
	struct part_table t;
	if (make_disk(cmd) || parse_part_table(head, sizeof head, 512, &t)) {
		unlink(disk);
		return -2;
	}
	unlink(disk);
	return find_core_place(&t, lbs, sectors, start);
}

static void an_mbr_disk_uses_the_gap_before_the_first_partition(void)
{
	uint64_t start = 0;
	CHECK(place_core("printf 'label: dos\\nstart=2048, type=83\\n' | sfdisk -q %s >/dev/null 2>&1", 252, 512,
			 &start) == 0);
	CHECK(start == 1);
}

static void an_old_63_sector_gap_is_too_small(void)
{
	uint64_t start = 0;
	CHECK(place_core("printf 'label: dos\\nstart=63, type=83\\n' | sfdisk -q %s >/dev/null 2>&1", 252, 512,
			 &start) == -1);
}

static void a_gpt_disk_prefers_its_bios_boot_partition(void)
{
	uint64_t start = 0;
	CHECK(place_core("sgdisk -o -n 1:4096:+1M -t 1:ef02 -n 2:0:0 -t 2:8300 %s >/dev/null 2>&1", 252, 512,
			 &start) == 0);
	CHECK(start == 4096);
}

static void a_gpt_disk_without_one_uses_the_gap_after_the_table(void)
{
	uint64_t start = 0;
	CHECK(place_core("sgdisk -o -n 1:2048:0 -t 1:8300 %s >/dev/null 2>&1", 252, 512, &start) == 0);
	CHECK(start == 34);
	CHECK(place_core("sgdisk -o -a 1 -n 1:34:0 -t 1:8300 %s >/dev/null 2>&1", 252, 512, &start) == -1);
}

static void four_k_sector_disks_cannot_start_grub_from_bios(void)
{
	uint64_t start = 0;
	CHECK(place_core("sgdisk -o -n 1:2048:0 -t 1:8300 %s >/dev/null 2>&1", 252, 4096, &start) == -1);
}

static void the_boot_sector_keeps_the_partition_table(void)
{
	uint8_t old[512], boot[512], out[512];
	for (int i = 0; i < 512; i++) {
		old[i] = (uint8_t)(i * 7);
		boot[i] = 0xcc;
	}
	build_boot_sector(old, boot, 0x0102030405ULL, out);
	CHECK(!memcmp(out + 0x1b8, old + 0x1b8, 512 - 0x1b8));
	CHECK(!memcmp(out + 3, old + 3, 0x5a - 3));
	CHECK(out[0] == 0xcc && out[0x100] == 0xcc);
	CHECK(out[0x5c] == 0x05 && out[0x5d] == 0x04 && out[0x60] == 0x01 && out[0x61] == 0);
	CHECK(out[0x64] == 0xff);
	CHECK(out[0x66] == 0x90 && out[0x67] == 0x90);
}

static void the_core_image_loads_its_remaining_sectors(void)
{
	uint8_t core[3 * 512 + 100];
	memset(core, 0xaa, sizeof core);
	apply_core_blocklist(core, sizeof core, 2048);
	const uint8_t *b = core + 0x1f4;
	CHECK(b[0] == 0x01 && b[1] == 0x08 && b[2] == 0 && b[7] == 0); /* start 2049 */
	CHECK(b[8] == 3 && b[9] == 0);                                  /* 3 more sectors */
	CHECK(b[10] == 0x20 && b[11] == 0x08);                          /* segment 0x820 */
	for (int i = 0x1e8; i < 0x1f4; i++)
		CHECK(core[i] == 0);
}

static void writes_grub_to_a_disk_and_puts_the_old_code_back(void)
{
	if (make_disk("printf 'label: dos\\nstart=2048, type=83\\n' | sfdisk -q %s >/dev/null 2>&1")) {
		printf("skip writes_grub_to_a_disk_and_puts_the_old_code_back (no sfdisk)\n");
		return;
	}
	char boot[] = "/tmp/bootimg-XXXXXX", core[] = "/tmp/coreimg-XXXXXX", backup[64];
	int fd = mkstemp(boot);
	uint8_t sector[512];
	memset(sector, 0xbb, sizeof sector);
	CHECK(write(fd, sector, 512) == 512);
	close(fd);
	fd = mkstemp(core);
	memset(sector, 0xcd, sizeof sector);
	for (int i = 0; i < 3; i++)
		CHECK(write(fd, sector, 512) == 512);
	close(fd);
	snprintf(backup, sizeof backup, "%s.mbr", disk);

	uint8_t before[512], after[512], written[512];
	fd = open(disk, O_RDWR);
	CHECK(pread(fd, before, 512, 0) == 512);
	before[0] = 0x33; /* the old boot code */
	CHECK(pwrite(fd, before, 512, 0) == 512);
	close(fd);

	CHECK(!write_bios_boot(disk, boot, core, 1, backup));
	fd = open(disk, O_RDONLY);
	CHECK(pread(fd, after, 512, 0) == 512);
	CHECK(pread(fd, written, 512, 512) == 512);
	close(fd);
	CHECK(after[0] == 0xbb && !memcmp(after + 0x1b8, before + 0x1b8, 512 - 0x1b8));
	CHECK(after[0x5c] == 1 && written[0] == 0xcd && written[0x1fc] == 2);

	char second[64];
	snprintf(second, sizeof second, "%s.second", disk);
	CHECK(!write_bios_boot(disk, boot, core, 1, second)); /* GRUB's own code is never backed up */
	CHECK(access(second, F_OK) != 0);
	CHECK(!write_bios_boot(disk, boot, core, 1, backup)); /* the backup keeps the first code */
	CHECK(!restore_bios_boot(disk, backup));
	fd = open(disk, O_RDONLY);
	CHECK(pread(fd, after, 512, 0) == 512);
	close(fd);
	CHECK(!memcmp(after, before, 512));
	unlink(disk);
	unlink(boot);
	unlink(core);
	unlink(backup);
}

int main(void)
{
	RUN(an_mbr_disk_uses_the_gap_before_the_first_partition);
	RUN(an_old_63_sector_gap_is_too_small);
	RUN(a_gpt_disk_prefers_its_bios_boot_partition);
	RUN(a_gpt_disk_without_one_uses_the_gap_after_the_table);
	RUN(four_k_sector_disks_cannot_start_grub_from_bios);
	RUN(the_boot_sector_keeps_the_partition_table);
	RUN(the_core_image_loads_its_remaining_sectors);
	RUN(writes_grub_to_a_disk_and_puts_the_old_code_back);
	return CHECK_DONE();
}
