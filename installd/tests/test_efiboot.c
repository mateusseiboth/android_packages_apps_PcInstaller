#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "efiboot.h"

static const struct efi_disk_location GPT_ESP = {
	.number = 1,
	.start = 2048,
	.size = 1048576,
	.signature = { 0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b },
	.mbr_type = 2,
	.signature_type = 2,
};

static uint16_t u16_at(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32_at(const unsigned char *p) { return u16_at(p) | (uint32_t)u16_at(p + 2) << 16; }
static uint64_t u64_at(const unsigned char *p) { return u32_at(p) | (uint64_t)u32_at(p + 4) << 32; }

static void encodes_an_active_load_option(void)
{
	unsigned char opt[512];
	size_t n = build_load_option(opt, sizeof opt, "LineageOS", &GPT_ESP, "\\EFI\\LineageOS\\grubx64.efi");
	/* attributes, path list length, "LineageOS\0" in UTF-16, HD(42) + File(4 + 27 * 2) + End(4) */
	const size_t desc = 10 * 2, hd = 42, file = 4 + 27 * 2, end = 4;
	CHECK(n == 4 + 2 + desc + hd + file + end);
	CHECK(u32_at(opt) == 1);
	CHECK(u16_at(opt + 4) == hd + file + end);
	CHECK(!memcmp(opt + 6, "L\0i\0n\0e\0a\0g\0e\0O\0S\0\0\0", desc));

	const unsigned char *p = opt + 6 + desc;
	CHECK(p[0] == 4 && p[1] == 1 && u16_at(p + 2) == hd);
	CHECK(u32_at(p + 4) == 1 && u64_at(p + 8) == 2048 && u64_at(p + 16) == 1048576);
	CHECK(!memcmp(p + 24, GPT_ESP.signature, 16) && p[40] == 2 && p[41] == 2);

	p += hd;
	CHECK(p[0] == 4 && p[1] == 4 && u16_at(p + 2) == file);
	CHECK(!memcmp(p + 4, "\\\0E\0F\0I\0\\\0", 10));
	CHECK(u16_at(p + file - 2) == 0);

	p += file;
	CHECK(p[0] == 0x7f && p[1] == 0xff && u16_at(p + 2) == 4);
}

static void a_small_buffer_gives_zero(void)
{
	unsigned char opt[32];
	CHECK(build_load_option(opt, sizeof opt, "LineageOS", &GPT_ESP, "\\EFI\\LineageOS\\grubx64.efi") == 0);
}

static void the_new_entry_boots_first_and_appears_once(void)
{
	uint16_t order[8] = { 0x0001, 0x0003, 0x0000 };
	int n = apply_boot_first(order, 3, 8, 0x0003);
	CHECK(n == 3 && order[0] == 0x0003 && order[1] == 0x0001 && order[2] == 0x0000);
	n = apply_boot_first(order, n, 8, 0x0007);
	CHECK(n == 4 && order[0] == 0x0007 && order[3] == 0x0000);
}

static void picks_the_lowest_free_boot_number(void)
{
	uint16_t used[] = { 0x0000, 0x0001, 0x0003 };
	CHECK(find_free_boot_number(used, 3) == 0x0002);
	CHECK(find_free_boot_number(used, 0) == 0x0000);
}

/* ---- efivarfs, against a plain directory ---------------------------------------------- */

static char dir[64];

static void make_dir(void)
{
	snprintf(dir, sizeof dir, "/tmp/efivars-XXXXXX");
	CHECK(mkdtemp(dir) != NULL);
}

static void put_var(const char *name, const void *data, size_t len)
{
	CHECK(!save_efi_var(dir, name, data, len));
}

static void remove_dir(void)
{
	DIR *d = opendir(dir);
	for (struct dirent *e; d && (e = readdir(d));)
		if (e->d_name[0] != '.')
			unlinkat(dirfd(d), e->d_name, 0);
	if (d)
		closedir(d);
	rmdir(dir);
}

static void variables_carry_their_attributes(void)
{
	make_dir();
	put_var("BootOrder", "\x01\x00", 2);
	char path[128];
	snprintf(path, sizeof path, "%s/BootOrder-" EFI_GLOBAL_GUID, dir);
	unsigned char raw[16];
	int fd = open(path, O_RDONLY);
	CHECK(fd >= 0 && read(fd, raw, sizeof raw) == 6);
	close(fd);
	CHECK(u32_at(raw) == 7 && raw[4] == 1 && raw[5] == 0);

	unsigned char back[16];
	size_t len = 0;
	CHECK(!read_efi_var(dir, "BootOrder", back, sizeof back, &len) && len == 2 && back[0] == 1);
	remove_dir();
}

static void adds_a_boot_entry_and_puts_it_first(void)
{
	make_dir();
	put_var("Boot0000", "windows", 7);
	put_var("Boot0001", "usb", 3);
	put_var("BootOrder", "\x00\x00\x01\x00", 4);
	unsigned char opt[512];
	size_t n = build_load_option(opt, sizeof opt, "LineageOS", &GPT_ESP, "\\EFI\\LineageOS\\grubx64.efi");

	uint16_t number = 0xffff;
	CHECK(!apply_boot_entry(dir, opt, n, &number));
	CHECK(number == 0x0002);
	unsigned char order[16];
	size_t len = 0;
	CHECK(!read_efi_var(dir, "BootOrder", order, sizeof order, &len));
	CHECK(len == 6 && u16_at(order) == 2 && u16_at(order + 2) == 0 && u16_at(order + 4) == 1);

	/* Installing again reuses the same entry. */
	CHECK(!apply_boot_entry(dir, opt, n, &number));
	CHECK(number == 0x0002);
	CHECK(!read_efi_var(dir, "BootOrder", order, sizeof order, &len) && len == 6);
	remove_dir();
}

static void works_without_a_boot_order(void)
{
	make_dir();
	unsigned char opt[512];
	size_t n = build_load_option(opt, sizeof opt, "LineageOS", &GPT_ESP, "\\EFI\\LineageOS\\grubx64.efi");
	uint16_t number = 0xffff;
	CHECK(!apply_boot_entry(dir, opt, n, &number) && number == 0);
	unsigned char order[16];
	size_t len = 0;
	CHECK(!read_efi_var(dir, "BootOrder", order, sizeof order, &len) && len == 2);
	remove_dir();
}

int main(void)
{
	RUN(encodes_an_active_load_option);
	RUN(a_small_buffer_gives_zero);
	RUN(the_new_entry_boots_first_and_appears_once);
	RUN(picks_the_lowest_free_boot_number);
	RUN(variables_carry_their_attributes);
	RUN(adds_a_boot_entry_and_puts_it_first);
	RUN(works_without_a_boot_order);
	return CHECK_DONE();
}
