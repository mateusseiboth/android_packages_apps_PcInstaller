#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "parttable.h"

static uint8_t head[PARTTABLE_HEAD_BYTES];

static int make_disk(const char *partition_cmd)
{
	char path[] = "/tmp/disk-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && !ftruncate(fd, 64 << 20));
	char cmd[512];
	snprintf(cmd, sizeof cmd, partition_cmd, path);
	int failed = system(cmd);
	ssize_t n = failed ? -1 : pread(fd, head, sizeof head, 0);
	close(fd);
	unlink(path);
	return n == (ssize_t)sizeof head ? 0 : -1;
}

static void reads_a_gpt_disk_and_finds_the_esp(void)
{
	if (make_disk("sgdisk -o -n 1:2048:+32M -t 1:ef00 -u 1:0f5a7f3c-1111-2222-3333-444455556666 "
		      "-n 2:0:0 -t 2:8300 %s >/dev/null 2>&1")) {
		printf("skip reads_a_gpt_disk_and_finds_the_esp (no sgdisk)\n");
		return;
	}
	struct part_table t;
	CHECK(!parse_part_table(head, sizeof head, 512, &t));
	CHECK(t.is_gpt && t.count == 2);
	CHECK(t.entries[0].number == 1 && t.entries[0].is_esp);
	CHECK(t.entries[0].start == 2048 && t.entries[0].size == 65536);
	CHECK(t.entries[1].number == 2 && !t.entries[1].is_esp);

	struct efi_disk_location loc;
	CHECK(!find_efi_location(&t, 1, &loc));
	CHECK(loc.mbr_type == 2 && loc.signature_type == 2 && loc.start == 2048 && loc.size == 65536);
	/* GUIDs are stored with the first three fields little-endian. */
	static const uint8_t GUID[16] = { 0x3c, 0x7f, 0x5a, 0x0f, 0x11, 0x11, 0x22, 0x22,
					  0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66 };
	CHECK(!memcmp(loc.signature, GUID, 16));
	CHECK(find_efi_location(&t, 9, &loc));
}

static void reads_an_mbr_disk(void)
{
	if (make_disk("printf 'label: dos\\nlabel-id: 0x1234abcd\\nstart=2048, size=65536, type=ef\\n"
		      "start=67584, type=83\\n' | sfdisk -q %s >/dev/null 2>&1")) {
		printf("skip reads_an_mbr_disk (no sfdisk)\n");
		return;
	}
	struct part_table t;
	CHECK(!parse_part_table(head, sizeof head, 512, &t));
	CHECK(!t.is_gpt && t.count == 2 && t.entries[0].is_esp && !t.entries[1].is_esp);
	struct efi_disk_location loc;
	CHECK(!find_efi_location(&t, 1, &loc));
	CHECK(loc.mbr_type == 1 && loc.signature_type == 1 && loc.start == 2048 && loc.size == 65536);
	CHECK(!memcmp(loc.signature, "\xcd\xab\x34\x12\0\0\0\0", 8));
}

static void a_blank_disk_has_no_table(void)
{
	struct part_table t;
	memset(head, 0, sizeof head);
	CHECK(parse_part_table(head, sizeof head, 512, &t));
}

int main(void)
{
	RUN(reads_a_gpt_disk_and_finds_the_esp);
	RUN(reads_an_mbr_disk);
	RUN(a_blank_disk_has_no_table);
	return CHECK_DONE();
}
