#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "fsprobe.h"

static unsigned char buf[PROBE_BYTES];

static void put(size_t off, const void *bytes, size_t len)
{
	memset(buf, 0, sizeof buf);
	memcpy(buf + off, bytes, len);
}

static void also_put(size_t off, const void *bytes, size_t len) { memcpy(buf + off, bytes, len); }

static const char *type_of(void)
{
	static struct fs_info fs;
	probe_fs(buf, sizeof buf, &fs);
	return fs.type;
}

static void finds_linux_file_systems(void)
{
	put(1080, "\x53\xef", 2);
	CHECK_STR(type_of(), "ext4");
	put(1024, "\x10\x20\xf5\xf2", 4);
	CHECK_STR(type_of(), "f2fs");
	put(65536 + 64, "_BHRfS_M", 8);
	CHECK_STR(type_of(), "btrfs");
	put(1024, "\xe2\xe1\xf5\xe0", 4);
	CHECK_STR(type_of(), "erofs");
}

static void finds_windows_file_systems(void)
{
	put(3, "NTFS    ", 8);
	CHECK_STR(type_of(), "ntfs");
	put(3, "EXFAT   ", 8);
	CHECK_STR(type_of(), "exfat");
	put(82, "FAT32   ", 8);
	CHECK_STR(type_of(), "vfat");
	put(54, "FAT16   ", 8);
	CHECK_STR(type_of(), "vfat");
}

static void finds_what_cannot_hold_an_install(void)
{
	put(3, "-FVE-FS-", 8);
	CHECK_STR(type_of(), "bitlocker");
	put(0, "LUKS\xba\xbe", 6);
	CHECK_STR(type_of(), "luks");
	put(32769, "CD001", 5);
	CHECK_STR(type_of(), "iso9660");
	put(4086, "SWAPSPACE2", 10);
	CHECK_STR(type_of(), "swap");
	put(512, "LABELONE", 8);
	CHECK_STR(type_of(), "lvm");
	put(0, "", 0);
	CHECK_STR(type_of(), "unknown");
}

static void reads_labels(void)
{
	struct fs_info fs;
	put(1080, "\x53\xef", 2);
	also_put(1024 + 120, "Linux", 5);
	CHECK(!probe_fs(buf, sizeof buf, &fs));
	CHECK_STR(fs.label, "Linux");

	put(82, "FAT32   ", 8);
	also_put(71, "PENDRIVE   ", 11);
	probe_fs(buf, sizeof buf, &fs);
	CHECK_STR(fs.label, "PENDRIVE");

	put(1024, "\x10\x20\xf5\xf2", 4);
	also_put(1024 + 124, "d\0a\0d\0o\0s\0", 10);
	probe_fs(buf, sizeof buf, &fs);
	CHECK_STR(fs.label, "dados");
}

static void a_short_read_is_unknown(void)
{
	struct fs_info fs;
	put(65536 + 64, "_BHRfS_M", 8);
	CHECK(probe_fs(buf, 4096, &fs));
	CHECK_STR(fs.type, "unknown");
}

static int probe_file(const char *path, struct fs_info *fs)
{
	FILE *f = fopen(path, "rb");
	size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
	if (f)
		fclose(f);
	return probe_fs(buf, n, fs);
}

static void probes_a_real_ext4_image(void)
{
	char path[] = "/tmp/fsprobe-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && !ftruncate(fd, 64 << 20));
	close(fd);
	char cmd[256];
	snprintf(cmd, sizeof cmd, "mke2fs -q -F -t ext4 -L sistema %s >/dev/null 2>&1", path);
	if (system(cmd)) {
		printf("skip probes_a_real_ext4_image (no mke2fs)\n");
		unlink(path);
		return;
	}
	struct fs_info fs;
	CHECK(!probe_file(path, &fs));
	CHECK_STR(fs.type, "ext4");
	CHECK_STR(fs.label, "sistema");
	unlink(path);
}

int main(void)
{
	RUN(finds_linux_file_systems);
	RUN(finds_windows_file_systems);
	RUN(finds_what_cannot_hold_an_install);
	RUN(reads_labels);
	RUN(a_short_read_is_unknown);
	RUN(probes_a_real_ext4_image);
	return CHECK_DONE();
}
