#include "fsprobe.h"

#include <stdio.h>
#include <string.h>

typedef void (*label_reader)(const unsigned char *src, size_t len, char *out, size_t cap);

static void read_ascii(const unsigned char *src, size_t len, char *out, size_t cap)
{
	size_t n = len < cap - 1 ? len : cap - 1;
	memcpy(out, src, n);
	out[n] = 0;
	out[strcspn(out, "\0")] = 0;
	for (size_t end = strlen(out); end && out[end - 1] == ' '; end--)
		out[end - 1] = 0;
}

/* UTF-16LE to UTF-8 for the Basic Multilingual Plane, which is what volume names use. */
static void read_utf16(const unsigned char *src, size_t len, char *out, size_t cap)
{
	size_t n = 0;
	for (size_t i = 0; i + 1 < len; i += 2) {
		unsigned c = src[i] | src[i + 1] << 8;
		char enc[4];
		int bytes = !c ? 0 : c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
		static const unsigned char LEAD[] = { 0, 0, 0xc0, 0xe0 };
		if (!bytes || n + (size_t)bytes >= cap)
			break;
		for (int k = bytes - 1; k > 0; k--, c >>= 6)
			enc[k] = (char)(0x80 | (c & 0x3f));
		enc[0] = (char)(LEAD[bytes] | c);
		memcpy(out + n, enc, (size_t)bytes);
		n += (size_t)bytes;
	}
	out[n] = 0;
}

/* Order matters: BitLocker carries a FAT-like boot sector, and LUKS or LVM can sit on anything. */
static const struct signature {
	const char *type;
	size_t magic_offset;
	const char *magic;
	size_t magic_len;
	size_t label_offset;
	size_t label_len;
	label_reader read_label;
} SIGNATURES[] = {
	{ "luks", 0, "LUKS\xba\xbe", 6, 0, 0, NULL },
	{ "bitlocker", 3, "-FVE-FS-", 8, 0, 0, NULL },
	{ "lvm", 512, "LABELONE", 8, 0, 0, NULL },
	{ "ntfs", 3, "NTFS    ", 8, 0, 0, NULL },
	{ "exfat", 3, "EXFAT   ", 8, 0, 0, NULL },
	{ "ext4", 1080, "\x53\xef", 2, 1024 + 120, 16, read_ascii },
	{ "f2fs", 1024, "\x10\x20\xf5\xf2", 4, 1024 + 124, 64, read_utf16 },
	{ "erofs", 1024, "\xe2\xe1\xf5\xe0", 4, 0, 0, NULL },
	{ "iso9660", 32769, "CD001", 5, 32808, 32, read_ascii },
	{ "btrfs", 65536 + 64, "_BHRfS_M", 8, 65536 + 299, 256, read_ascii },
	{ "vfat", 82, "FAT32   ", 8, 71, 11, read_ascii },
	{ "vfat", 54, "FAT16   ", 8, 43, 11, read_ascii },
	{ "vfat", 54, "FAT12   ", 8, 43, 11, read_ascii },
	{ "swap", 4086, "SWAPSPACE2", 10, 1024 + 28, 16, read_ascii },
};

static int matches(const struct signature *s, const unsigned char *buf, size_t len)
{
	return s->magic_offset + s->magic_len <= len && !memcmp(buf + s->magic_offset, s->magic, s->magic_len);
}

static void apply_signature(const struct signature *s, const unsigned char *buf, size_t len, struct fs_info *out)
{
	snprintf(out->type, sizeof out->type, "%s", s->type);
	if (s->read_label && s->label_offset + s->label_len <= len)
		s->read_label(buf + s->label_offset, s->label_len, out->label, sizeof out->label);
}

int probe_fs(const unsigned char *buf, size_t len, struct fs_info *out)
{
	*out = (struct fs_info){ .type = "unknown" };
	for (size_t i = 0; i < sizeof SIGNATURES / sizeof *SIGNATURES; i++) {
		if (!matches(&SIGNATURES[i], buf, len))
			continue;
		apply_signature(&SIGNATURES[i], buf, len, out);
		return 0;
	}
	return -1;
}
