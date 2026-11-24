#include "efiboot.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <unistd.h>

#define LOAD_OPTION_ACTIVE 0x1
#define VAR_ATTRIBUTES 0x7 /* non-volatile, boot service and runtime access */
#define EFIVARFS_MAGIC 0xde5e81e4
#define MAX_BOOT_ENTRIES 512

/* ---- encoding ------------------------------------------------------------------------- */

struct out {
	uint8_t *p;
	size_t cap, len;
};

static void put_bytes(struct out *o, const void *bytes, size_t n)
{
	if (o->len + n <= o->cap)
		memcpy(o->p + o->len, bytes, n);
	o->len += n;
}

static void put_le(struct out *o, uint64_t v, size_t n)
{
	uint8_t b[8];
	for (size_t i = 0; i < n; i++, v >>= 8)
		b[i] = (uint8_t)v;
	put_bytes(o, b, n);
}

static void put_utf16(struct out *o, const char *ascii)
{
	do
		put_le(o, (uint8_t)*ascii, 2);
	while (*ascii++);
}

static void put_node_header(struct out *o, uint8_t type, uint8_t subtype, size_t len)
{
	put_le(o, type, 1);
	put_le(o, subtype, 1);
	put_le(o, len, 2);
}

static void put_hard_drive(struct out *o, const struct efi_disk_location *loc)
{
	put_node_header(o, 4, 1, 42);
	put_le(o, loc->number, 4);
	put_le(o, loc->start, 8);
	put_le(o, loc->size, 8);
	put_bytes(o, loc->signature, 16);
	put_le(o, loc->mbr_type, 1);
	put_le(o, loc->signature_type, 1);
}

static void put_file_path(struct out *o, const char *path)
{
	put_node_header(o, 4, 4, 4 + 2 * (strlen(path) + 1));
	put_utf16(o, path);
}

static void put_end(struct out *o) { put_node_header(o, 0x7f, 0xff, 4); }

size_t build_load_option(uint8_t *out, size_t cap, const char *description, const struct efi_disk_location *loc,
			 const char *path)
{
	size_t path_list = 42 + 4 + 2 * (strlen(path) + 1) + 4;
	struct out o = { out, cap, 0 };
	put_le(&o, LOAD_OPTION_ACTIVE, 4);
	put_le(&o, path_list, 2);
	put_utf16(&o, description);
	put_hard_drive(&o, loc);
	put_file_path(&o, path);
	put_end(&o);
	return o.len <= cap ? o.len : 0;
}

/* ---- boot order ----------------------------------------------------------------------- */

int apply_boot_first(uint16_t *order, int count, int max, uint16_t number)
{
	int kept = 0;
	for (int i = 0; i < count; i++)
		if (order[i] != number)
			order[kept++] = order[i];
	kept = kept < max ? kept : max - 1;
	memmove(order + 1, order, (size_t)kept * sizeof *order);
	order[0] = number;
	return kept + 1;
}

static int is_used(const uint16_t *used, int count, int n)
{
	for (int i = 0; i < count; i++)
		if (used[i] == n)
			return 1;
	return 0;
}

int find_free_boot_number(const uint16_t *used, int count)
{
	int n = 0;
	while (is_used(used, count, n))
		n++;
	return n;
}

/* ---- efivarfs ------------------------------------------------------------------------- */

static void build_var_path(char *out, size_t cap, const char *dir, const char *name)
{
	snprintf(out, cap, "%s/%s-%s", dir, name, EFI_GLOBAL_GUID);
}

/* efivarfs marks variables immutable; writing one needs the flag cleared first. */
static void clear_immutable(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	int flags = 0;
	if (fd < 0)
		return;
	if (!ioctl(fd, FS_IOC_GETFLAGS, &flags) && flags & FS_IMMUTABLE_FL) {
		flags &= ~FS_IMMUTABLE_FL;
		ioctl(fd, FS_IOC_SETFLAGS, &flags);
	}
	close(fd);
}

/* efivarfs replaces a variable with each write; a plain directory (the tests) needs truncating. */
static int find_truncate_flag(const char *dir)
{
	struct statfs st;
	return !statfs(dir, &st) && (unsigned long)st.f_type == EFIVARFS_MAGIC ? 0 : O_TRUNC;
}

int save_efi_var(const char *dir, const char *name, const void *data, size_t len)
{
	char path[512];
	uint8_t buf[4096];
	if (len + 4 > sizeof buf)
		return -1;
	build_var_path(path, sizeof path, dir, name);
	clear_immutable(path);
	uint32_t attrs = VAR_ATTRIBUTES;
	memcpy(buf, &attrs, 4);
	memcpy(buf + 4, data, len);
	int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | find_truncate_flag(dir), 0644);
	if (fd < 0)
		return -1;
	ssize_t n = write(fd, buf, len + 4);
	int closed = close(fd);
	return n == (ssize_t)(len + 4) && !closed ? 0 : -1;
}

int read_efi_var(const char *dir, const char *name, void *data, size_t cap, size_t *len)
{
	char path[512];
	uint8_t buf[4096];
	build_var_path(path, sizeof path, dir, name);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, buf, sizeof buf) : -1;
	if (fd >= 0)
		close(fd);
	if (n < 4 || (size_t)n - 4 > cap)
		return -1;
	*len = (size_t)n - 4;
	memcpy(data, buf + 4, *len);
	return 0;
}

/* Boot#### with four upper-case hex digits; BootOrder, BootNext and BootCurrent are not entries. */
static int parse_boot_name(const char *file, uint16_t *number)
{
	static const char SUFFIX[] = "-" EFI_GLOBAL_GUID;
	if (strncmp(file, "Boot", 4) || strlen(file) != 8 + strlen(SUFFIX) || strcmp(file + 8, SUFFIX))
		return -1;
	for (int i = 4; i < 8; i++)
		if (!isxdigit((unsigned char)file[i]) || islower((unsigned char)file[i]))
			return -1;
	*number = (uint16_t)strtoul((char[5]){ file[4], file[5], file[6], file[7], 0 }, NULL, 16);
	return 0;
}

static int has_same_option(const char *dir, uint16_t number, const uint8_t *option, size_t len)
{
	char name[16];
	uint8_t buf[4096];
	size_t n = 0;
	snprintf(name, sizeof name, "Boot%04X", number);
	return !read_efi_var(dir, name, buf, sizeof buf, &n) && n == len && !memcmp(buf, option, len);
}

/* Lists the existing entries; returns the one already holding option, or -1. */
static int find_boot_entries(const char *dir, const uint8_t *option, size_t len, uint16_t *used, int *count)
{
	DIR *d = opendir(dir);
	int same = -1;
	uint16_t number;
	*count = 0;
	for (struct dirent *e; d && (e = readdir(d)) && *count < MAX_BOOT_ENTRIES;) {
		if (parse_boot_name(e->d_name, &number))
			continue;
		used[(*count)++] = number;
		same = same < 0 && has_same_option(dir, number, option, len) ? number : same;
	}
	if (d)
		closedir(d);
	return same;
}

static int save_boot_order(const char *dir, uint16_t number)
{
	uint16_t order[MAX_BOOT_ENTRIES];
	size_t len = 0;
	int count = read_efi_var(dir, "BootOrder", order, sizeof order, &len) ? 0 : (int)(len / 2);
	count = apply_boot_first(order, count, MAX_BOOT_ENTRIES, number);
	return save_efi_var(dir, "BootOrder", order, (size_t)count * 2);
}

int apply_boot_entry(const char *dir, const uint8_t *option, size_t len, uint16_t *number)
{
	uint16_t used[MAX_BOOT_ENTRIES];
	int count = 0;
	int same = find_boot_entries(dir, option, len, used, &count);
	*number = (uint16_t)(same >= 0 ? same : find_free_boot_number(used, count));
	char name[16];
	snprintf(name, sizeof name, "Boot%04X", *number);
	if (same < 0 && save_efi_var(dir, name, option, len))
		return -1;
	return save_boot_order(dir, *number);
}

/* ---- removing ------------------------------------------------------------------------- */

/* The description is UTF-16 after the attributes (4) and the path list length (2). */
static int has_description(const uint8_t *option, size_t len, const char *description)
{
	size_t i = 0;
	for (; description[i]; i++)
		if (6 + 2 * i + 1 >= len || option[6 + 2 * i] != (uint8_t)description[i] || option[7 + 2 * i])
			return 0;
	return 6 + 2 * i + 1 < len && !option[6 + 2 * i] && !option[7 + 2 * i];
}

static int remove_boot_var(const char *dir, uint16_t number, const char *description)
{
	char name[16], path[512];
	uint8_t buf[4096];
	size_t n = 0;
	snprintf(name, sizeof name, "Boot%04X", number);
	if (read_efi_var(dir, name, buf, sizeof buf, &n) || !has_description(buf, n, description))
		return 0;
	build_var_path(path, sizeof path, dir, name);
	clear_immutable(path);
	return unlink(path) ? 0 : 1;
}

static int is_removed(const uint16_t *removed, int count, uint16_t number) { return is_used(removed, count, number); }

static int save_order_without(const char *dir, const uint16_t *removed, int count)
{
	uint16_t order[MAX_BOOT_ENTRIES];
	size_t len = 0;
	if (read_efi_var(dir, "BootOrder", order, sizeof order, &len))
		return 0;
	int kept = 0;
	for (size_t i = 0; i < len / 2; i++)
		if (!is_removed(removed, count, order[i]))
			order[kept++] = order[i];
	return save_efi_var(dir, "BootOrder", order, (size_t)kept * 2);
}

int remove_boot_entries(const char *dir, const char *description)
{
	uint16_t used[MAX_BOOT_ENTRIES], removed[MAX_BOOT_ENTRIES];
	int count = 0, gone = 0;
	find_boot_entries(dir, (const uint8_t *)"", 0, used, &count);
	for (int i = 0; i < count; i++)
		if (remove_boot_var(dir, used[i], description))
			removed[gone++] = used[i];
	if (gone && save_order_without(dir, removed, gone))
		return -1;
	return gone;
}
