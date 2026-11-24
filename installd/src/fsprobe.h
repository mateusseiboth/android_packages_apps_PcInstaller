/* Which file system a partition holds, from the signatures in its first bytes (a small blkid). */
#pragma once
#include <stddef.h>

/* Enough to reach the btrfs superblock at 64 KiB. */
#define PROBE_BYTES (68 * 1024)

struct fs_info {
	char type[16]; /* ext4 f2fs btrfs erofs ntfs exfat vfat iso9660 swap bitlocker luks lvm raid unknown */
	char label[64];
	char uuid[40]; /* as GRUB's search --fs-uuid prints it; empty when GRUB has none for the type */
};

/* 0 when a signature matched; otherwise -1 with type "unknown". */
int probe_fs(const unsigned char *buf, size_t len, struct fs_info *out);
