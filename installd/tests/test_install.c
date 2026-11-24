#include <sys/stat.h>

#include "check.h"
#include "disks.h"
#include "efiboot.h"
#include "fakesys.h"
#include "install.h"
#include "util.h"

static struct live_source source;
static struct partition target, esp, windows, live;
static struct partition parts[4];
static int last_percent, went_back;

static void setup_machine(void)
{
	fake_setup();
	fake_disk("sda", 512);
	fake_partition("sda", "sda1", 1, 65536);
	fake_partition("sda", "sda2", 2, 65536);
	fake_partition("sda", "sda3", 3, 1048576);
	fake_partition("sdb", "sdb1", 1, 65536);
	fake_sh("truncate -s 64M dev/block/%s && sgdisk -o -n 1:2048:+32M -t 1:ef00 -n 3:0:0 -t 3:8300 "
		"dev/block/sda >/dev/null", "sda");
	fake_partition("sda", "loop9", 0, 0);
	fake_sh("mkdir -p mnt-sdb1/%1$s && printf KERNEL > mnt-sdb1/%1$s/kernel && printf INITRD > mnt-sdb1/%1$s/initrd.img"
		" && printf GRUB > mnt-sdb1/%1$s/grubx64.efi && printf SHIM > mnt-sdb1/%1$s/shimx64.efi"
		" && printf MM > mnt-sdb1/%1$s/mmx64.efi && printf DER > mnt-sdb1/%1$s/ENROLL_THIS_KEY_IN_MOKMANAGER.der"
		" && truncate -s 32M mnt-sdb1/%1$s/system.img"
		" && printf NEW | dd conv=notrunc of=mnt-sdb1/%1$s/system.img 2>/dev/null", "lineage");
	/* GRUB for BIOS: boot.img (one sector of 0xbb), core.img (three of 0xcd), a module. */
	fake_sh("mkdir -p mnt-sdb1/%1$s/grub-pc/i386-pc && printf normal > mnt-sdb1/%1$s/grub-pc/i386-pc/normal.mod"
		" && head -c 512 /dev/zero | tr '\\0' '\\273' > mnt-sdb1/%1$s/grub-pc/boot.img"
		" && head -c 1536 /dev/zero | tr '\\0' '\\315' > mnt-sdb1/%1$s/grub-pc/core.img", "lineage");
	/* What the loop-mounted system image holds. */
	fake_sh("mkdir -p mnt-loop9/system/bin && printf init > mnt-loop9/system/bin/init && ln -s /system/bin/init %s",
		"mnt-loop9/init");
	source = (struct live_source){ .name = "sdb1", .disk = "sdb", .folder = "lineage",
				       .sizes = { 6, 6, 32 << 20, 4 } };
	snprintf(source.dir, sizeof source.dir, "%s/mnt-sdb1/lineage", fake_root);
	target = (struct partition){ .name = "sda3", .disk = "sda", .fs = "ext4", .number = 3, .free = 1ULL << 40,
				     .uuid = "0f5a7f3c-1111-2222-3333-444455556666" };
	esp = (struct partition){ .name = "sda1", .disk = "sda", .fs = "vfat", .number = 1, .is_esp = 1,
				  .uuid = "1234-abcd" };
	windows = (struct partition){ .name = "sda2", .disk = "sda", .fs = "ntfs", .number = 2, .uuid = "0102030405060708" };
	live = (struct partition){ .name = "sdb1", .disk = "sdb", .fs = "vfat", .number = 1, .is_source = 1,
				   .uuid = "aaaa-bbbb" };
	parts[0] = esp;
	parts[1] = windows;
	parts[2] = target;
	parts[3] = live;
	fake_sh("mkdir -p mnt-sdb1/boot/grub && touch %s", "mnt-sdb1/boot/grub/grub.cfg");
	last_percent = -1;
	went_back = 0;
	fake_efivars_mounts = 0;
}

static void on_progress(void *ctx, const char *step, int percent)
{
	(void)ctx;
	(void)step;
	went_back |= percent < last_percent;
	last_percent = percent;
}

static struct install_job make_job(const char *folder, const char *id, const char *system, const char *data,
				   unsigned long long data_size, int is_uefi)
{
	struct install_job job = { .env = &fake_env, .ops = &FAKE_OPS, .src = &source, .target = &target,
				   .esp = is_uefi ? &esp : NULL, .parts = parts, .count = 4, .is_uefi = is_uefi,
				   .req = { .part = "sda3", .system_form = system, .data_form = data, .data_size = data_size } };
	snprintf(job.req.folder, sizeof job.req.folder, "%s", folder);
	snprintf(job.id, sizeof job.id, "%s", id);
	return job;
}

static const char *install_as(const char *folder, const char *id, const char *system, const char *data, int is_uefi)
{
	struct install_job job = make_job(folder, id, system, data, 2ULL << 30, is_uefi);
	const char *error = NULL;
	return run_install(&job, on_progress, NULL, &error) ? error : "ok";
}

static const char *install(const char *system, const char *data, unsigned long long data_size)
{
	struct install_job job = make_job("lineage", "0011223344556677", system, data, data_size, 1);
	const char *error = NULL;
	return run_install(&job, on_progress, NULL, &error) ? error : "ok";
}

static const char *uninstall(const char *folder, int is_uefi)
{
	struct install_job job = make_job(folder, "", "image", "keep", 0, is_uefi);
	const char *error = NULL;
	return run_uninstall(&job, on_progress, NULL, &error) ? error : "ok";
}

static void read_sector(uint64_t lba, unsigned char *out)
{
	FILE *f = fopen(fake_path("dev/block/sda"), "rb");
	CHECK(f && !fseek(f, (long)(lba * 512), SEEK_SET) && fread(out, 1, 512, f) == 512);
	if (f)
		fclose(f);
}

static int exists(const char *rel) { struct stat st; return !lstat(fake_path(rel), &st); }

static void read_rel(const char *rel, char *out, size_t cap)
{
	FILE *f = fopen(fake_path(rel), "r");
	size_t n = f ? fread(out, 1, cap - 1, f) : 0;
	out[n] = 0;
	if (f)
		fclose(f);
}

static const char *read_file(const char *rel)
{
	static char text[4][256];
	static int next;
	char *out = text[next++ % 4];
	read_rel(rel, out, sizeof text[0]);
	return out;
}

static size_t build_esp_option(const char *path, unsigned char *out, size_t cap)
{
	struct part_table t;
	struct efi_disk_location loc;
	CHECK(!read_part_table(&fake_env, "sda", &t) && !find_efi_location(&t, 1, &loc));
	return build_load_option(out, cap, "LineageOS", &loc, path);
}

static int has_boot_path(int number, const char *path)
{
	char name[16];
	unsigned char want[1024], got[1024];
	size_t n = build_esp_option(path, want, sizeof want), len = 0;
	snprintf(name, sizeof name, "Boot%04X", number);
	return !read_efi_var(fake_env.efivars, name, got, sizeof got, &len) && len == n && !memcmp(got, want, n);
}

static void installs_images_and_the_boot_entry(void)
{
	setup_machine();
	CHECK_STR(install("image", "image", 2ULL << 30), "ok");
	char text[4096];
	read_rel("mnt-sda3/lineage/kernel", text, sizeof text);
	CHECK_STR(text, "KERNEL");
	read_rel("mnt-sda3/lineage/initrd.img", text, sizeof text);
	CHECK_STR(text, "INITRD");
	read_rel("mnt-sda3/lineage/system.img", text, sizeof text);
	CHECK_STR(text, "NEW");
	struct stat st;
	CHECK(!stat(fake_path("mnt-sda3/lineage/data.img"), &st) && st.st_size == 2LL << 30);
	CHECK(exists("mnt-sda3/lineage/install-0011223344556677.id"));
	CHECK(!exists("mnt-sda3/lineage/system.img.tmp"));

	CHECK_STR(read_file("mnt-sda1/EFI/LineageOS/grubx64.efi"), "GRUB");
	CHECK_STR(read_file("mnt-sda1/EFI/LineageOS/shimx64.efi"), "SHIM");
	CHECK_STR(read_file("mnt-sda1/EFI/LineageOS/mmx64.efi"), "MM");
	CHECK_STR(read_file("mnt-sda1/EFI/LineageOS/ENROLL_THIS_KEY_IN_MOKMANAGER.der"), "DER");
	read_rel("mnt-sda1/EFI/LineageOS/grub.cfg", text, sizeof text);
	CHECK(strstr(text, "--file /lineage/install-0011223344556677.id") != NULL);
	CHECK(strstr(text, "menuentry \"LineageOS (sda3)\" --id lineage-0011223344556677") != NULL);
	CHECK(!strstr(text, "pc.previous=1"));
	CHECK(exists("mnt-sda1/EFI/LineageOS/entries/0011223344556677.cfg"));
	/* shim looks for GRUB and MokManager next to itself */
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/BOOTX64.EFI"), "SHIM");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/grubx64.efi"), "GRUB");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/mmx64.efi"), "MM");
	CHECK(fake_efivars_mounts == 1);
	CHECK(exists("efivars/Boot0000-" EFI_GLOBAL_GUID));
	CHECK(has_boot_path(0, "\\EFI\\LineageOS\\shimx64.efi"));
	CHECK(exists("efivars/BootOrder-" EFI_GLOBAL_GUID));

	CHECK(last_percent == 100 && !went_back);
	CHECK(is_dir(fake_path("work/target")) && is_dir(fake_path("work/esp")));
	fake_teardown();
}

static void extracts_the_system_and_makes_a_data_folder(void)
{
	setup_machine();
	CHECK_STR(install("folder", "folder", 0), "ok");
	char text[64];
	read_rel("mnt-sda3/lineage/system/system/bin/init", text, sizeof text);
	CHECK_STR(text, "init");
	char link[64] = { 0 };
	CHECK(readlink(fake_path("mnt-sda3/lineage/system/init"), link, sizeof link - 1) > 0);
	CHECK_STR(link, "/system/bin/init");
	struct stat st;
	CHECK(!stat(fake_path("mnt-sda3/lineage/userdata"), &st) && (st.st_mode & 07777) == 0771);
	CHECK(!exists("mnt-sda3/lineage/system.img"));
	CHECK(is_dir(fake_path("work/image")));
	fake_teardown();
}

static void keeps_an_existing_boot_loader_at_the_fallback_path(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda1/EFI/BOOT && printf WINDOWS > %s", "mnt-sda1/EFI/BOOT/BOOTX64.EFI");
	CHECK_STR(install("image", "folder", 0), "ok");
	char text[64];
	read_rel("mnt-sda1/EFI/BOOT/BOOTX64.EFI", text, sizeof text);
	CHECK_STR(text, "WINDOWS");
	fake_teardown();
}

static void a_reinstall_moves_the_boot_entry_from_grub_to_shim(void)
{
	setup_machine();
	unsigned char old[1024];
	size_t n = build_esp_option("\\EFI\\LineageOS\\grubx64.efi", old, sizeof old);
	uint16_t number = 0;
	CHECK(!save_efi_var(fake_env.efivars, "Boot0003", "windows", 7));
	CHECK(!apply_boot_entry(fake_env.efivars, old, n, &number) && number == 0);
	CHECK_STR(install("image", "folder", 0), "ok");
	CHECK(!exists("efivars/Boot0000-" EFI_GLOBAL_GUID) || has_boot_path(0, "\\EFI\\LineageOS\\shimx64.efi"));
	CHECK(has_boot_path(0, "\\EFI\\LineageOS\\shimx64.efi") || has_boot_path(1, "\\EFI\\LineageOS\\shimx64.efi"));
	CHECK(!has_boot_path(0, "\\EFI\\LineageOS\\grubx64.efi") && !has_boot_path(1, "\\EFI\\LineageOS\\grubx64.efi"));
	CHECK(exists("efivars/Boot0003-" EFI_GLOBAL_GUID));
	uint16_t order[8];
	size_t len = 0;
	CHECK(!read_efi_var(fake_env.efivars, "BootOrder", order, sizeof order, &len) && len == 2);
	CHECK(has_boot_path(order[0], "\\EFI\\LineageOS\\shimx64.efi"));
	fake_teardown();
}

/* Our old fallback (our GRUB as BOOTX64.EFI) and the current one (GRUB next to shim) are ours to replace. */
static void our_fallback_loader_is_brought_up_to_date(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda1/EFI/BOOT mnt-sda1/EFI/LineageOS && printf OLDGRUB > mnt-sda1/EFI/LineageOS/grubx64.efi"
		" && printf OLDGRUB > %s", "mnt-sda1/EFI/BOOT/BOOTX64.EFI");
	CHECK_STR(install("image", "folder", 0), "ok");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/BOOTX64.EFI"), "SHIM");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/grubx64.efi"), "GRUB");
	fake_teardown();

	setup_machine();
	fake_sh("mkdir -p mnt-sda1/EFI/BOOT mnt-sda1/EFI/LineageOS && printf OLDGRUB > mnt-sda1/EFI/LineageOS/grubx64.efi"
		" && printf OLDGRUB > mnt-sda1/EFI/BOOT/grubx64.efi && printf OLDSHIM > %s", "mnt-sda1/EFI/BOOT/BOOTX64.EFI");
	CHECK_STR(install("image", "folder", 0), "ok");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/BOOTX64.EFI"), "SHIM");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/grubx64.efi"), "GRUB");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/mmx64.efi"), "MM");
	fake_teardown();
}

/* Fedora puts the same Microsoft-signed shim at EFI/BOOT; that one is not ours. */
static void another_systems_shim_at_the_fallback_path_stays(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda1/EFI/BOOT && printf FALLBACK > mnt-sda1/EFI/BOOT/fbx64.efi && printf SHIM > %s",
		"mnt-sda1/EFI/BOOT/BOOTX64.EFI");
	CHECK_STR(install("image", "folder", 0), "ok");
	CHECK(!exists("mnt-sda1/EFI/BOOT/grubx64.efi") && !exists("mnt-sda1/EFI/BOOT/mmx64.efi"));
	CHECK_STR(uninstall("lineage", 1), "ok");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/BOOTX64.EFI"), "SHIM");
	CHECK_STR(read_file("mnt-sda1/EFI/BOOT/fbx64.efi"), "FALLBACK");
	fake_teardown();
}

static void an_update_keeps_the_previous_system_and_the_data(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda3/lineage/userdata && printf OLD > mnt-sda3/lineage/system.img && printf V > %s",
		"mnt-sda3/lineage/userdata/f");
	fake_sh("printf OLDER > mnt-sda3/lineage/system.old.img && touch %s", "mnt-sda3/lineage/install-aaaa.id");
	CHECK_STR(install("image", "keep", 0), "ok");
	char text[4096];
	read_rel("mnt-sda3/lineage/system.old.img", text, sizeof text);
	CHECK_STR(text, "OLD");
	read_rel("mnt-sda3/lineage/system.img", text, sizeof text);
	CHECK_STR(text, "NEW");
	read_rel("mnt-sda3/lineage/userdata/f", text, sizeof text);
	CHECK_STR(text, "V");
	CHECK(!exists("mnt-sda3/lineage/install-aaaa.id"));
	read_rel("mnt-sda1/EFI/LineageOS/grub.cfg", text, sizeof text);
	CHECK(strstr(text, "pc.previous=1") != NULL);
	fake_teardown();
}

static void an_update_can_change_the_system_form(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda3/lineage/system mnt-sda3/lineage/userdata && touch %s", "mnt-sda3/lineage/system/init");
	CHECK_STR(install("image", "keep", 0), "ok");
	CHECK(exists("mnt-sda3/lineage/system.old/init"));
	CHECK(exists("mnt-sda3/lineage/system.img"));
	CHECK(!exists("mnt-sda3/lineage/system"));
	fake_teardown();
}

static void a_failed_copy_puts_the_old_system_back(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda3/lineage/userdata && printf OLD > %s", "mnt-sda3/lineage/system.img");
	fake_sh("printf OLDER > %s", "mnt-sda3/lineage/system.old.img");
	fake_sh("rm %s", "mnt-sdb1/lineage/system.img");
	CHECK_STR(install("image", "keep", 0), "copy_failed");
	char text[64];
	read_rel("mnt-sda3/lineage/system.img", text, sizeof text);
	CHECK_STR(text, "OLD");
	CHECK(!exists("mnt-sda3/lineage/system.img.tmp"));
	CHECK(is_dir(fake_path("work/target")));
	fake_teardown();
}

static void reads_what_an_install_folder_holds(void)
{
	setup_machine();
	struct install_state s;
	read_install_state(fake_path("mnt-sda3/lineage"), &s);
	CHECK(!s.has_system && !s.has_data);
	fake_sh("mkdir -p mnt-sda3/lineage/system.old/bin && truncate -s 1M %s && touch mnt-sda3/lineage/data.img",
		"mnt-sda3/lineage/system.old/bin/x");
	fake_sh("mkdir -p %s", "mnt-sda3/lineage/system");
	fake_sh("touch %s", "mnt-sda3/lineage/install-00ff.id");
	read_install_state(fake_path("mnt-sda3/lineage"), &s);
	CHECK(s.has_system && s.has_data && s.old_system_bytes == 1 << 20);
	CHECK_STR(s.id, "00ff");
	fake_teardown();
}

/* GRUB's own parser accepts the menu (when grub2-script-check is installed). */
static int is_valid_grub_script(const char *rel)
{
	char cmd[512];
	snprintf(cmd, sizeof cmd, "command -v grub2-script-check >/dev/null || exit 0; grub2-script-check %s", fake_path(rel));
	return !system(cmd);
}

static void every_install_is_in_the_one_menu(void)
{
	setup_machine();
	CHECK_STR(install_as("lineage", "aaaa", "image", "folder", 1), "ok");
	CHECK_STR(install_as("lineage2", "bbbb", "image", "folder", 1), "ok");
	char text[16384];
	read_rel("mnt-sda1/EFI/LineageOS/grub.cfg", text, sizeof text);
	CHECK(strstr(text, "--file /lineage/install-aaaa.id") != NULL);
	CHECK(strstr(text, "--file /lineage2/install-bbbb.id") != NULL);
	CHECK(strstr(text, "set default=lineage-bbbb\n") != NULL);
	CHECK(is_valid_grub_script("mnt-sda1/EFI/LineageOS/grub.cfg"));
	fake_teardown();
}

static void the_menu_offers_the_other_systems(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda1/EFI/Microsoft/Boot mnt-sda1/EFI/fedora && touch mnt-sda1/EFI/fedora/shimx64.efi %s",
		"mnt-sda1/EFI/Microsoft/Boot/bootmgfw.efi");
	CHECK_STR(install("image", "folder", 0), "ok");
	char text[16384];
	read_rel("mnt-sda1/EFI/LineageOS/grub.cfg", text, sizeof text);
	CHECK(strstr(text, "menuentry \"Windows Boot Manager\"") != NULL);
	CHECK(strstr(text, "--fs-uuid 1234-abcd\n    chainloader /EFI/Microsoft/Boot/bootmgfw.efi") != NULL);
	CHECK(strstr(text, "chainloader /EFI/fedora/shimx64.efi") != NULL);
	CHECK(!strstr(text, "chainloader /EFI/LineageOS"));
	CHECK(strstr(text, "fwsetup") != NULL);
	CHECK(is_valid_grub_script("mnt-sda1/EFI/LineageOS/grub.cfg"));
	fake_teardown();
}

static void a_bios_computer_gets_grub_in_the_disk_gap(void)
{
	setup_machine();
	unsigned char before[512], sector[512];
	read_sector(0, before);
	fake_sh("touch %s", "mnt-sda2/bootmgr");
	CHECK_STR(install_as("lineage", "aaaa", "image", "folder", 0), "ok");
	read_sector(0, sector);
	CHECK(sector[0] == 0xbb && sector[0x5c] == 34 && !memcmp(sector + 0x1b8, before + 0x1b8, 512 - 0x1b8));
	read_sector(34, sector);
	CHECK(sector[0] == 0xcd && sector[0x1f4] == 35 && sector[0x1fc] == 2);
	CHECK(exists("mnt-sda3/pc-grub/i386-pc/normal.mod"));
	CHECK(exists("mnt-sda3/pc-grub/mbr-sda.bak"));
	char text[16384];
	read_rel("mnt-sda3/pc-grub/grub.cfg", text, sizeof text);
	CHECK(strstr(text, "--file /lineage/install-aaaa.id") != NULL);
	CHECK(strstr(text, "menuentry \"Windows (sda2)\"") != NULL);
	CHECK(strstr(text, "chainloader +1") != NULL);
	CHECK(!strstr(text, "fwsetup") && !strstr(text, "aaaa-bbbb"));
	CHECK(is_valid_grub_script("mnt-sda3/pc-grub/grub.cfg"));
	CHECK(fake_efivars_mounts == 0 && !exists("mnt-sda1/EFI/LineageOS"));
	fake_teardown();
}

static void uninstalling_removes_the_install_its_entry_and_our_boot_loader(void)
{
	setup_machine();
	CHECK_STR(install("image", "image", 2ULL << 30), "ok");
	CHECK_STR(uninstall("lineage", 1), "ok");
	CHECK(!exists("mnt-sda3/lineage"));
	CHECK(!exists("mnt-sda1/EFI/LineageOS"));
	CHECK(!exists("mnt-sda1/EFI/BOOT/BOOTX64.EFI") && !exists("mnt-sda1/EFI/BOOT/grubx64.efi"));
	CHECK(!exists("mnt-sda1/EFI/BOOT/mmx64.efi"));
	CHECK(!exists("efivars/Boot0000-" EFI_GLOBAL_GUID));
	CHECK(last_percent == 100);
	fake_teardown();
}

static void uninstalling_one_install_keeps_the_others(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda1/EFI/BOOT && printf WINDOWS > %s", "mnt-sda1/EFI/BOOT/BOOTX64.EFI");
	CHECK_STR(install_as("lineage", "aaaa", "image", "folder", 1), "ok");
	CHECK_STR(install_as("lineage2", "bbbb", "image", "folder", 1), "ok");
	CHECK_STR(uninstall("lineage", 1), "ok");
	char text[16384];
	read_rel("mnt-sda1/EFI/LineageOS/grub.cfg", text, sizeof text);
	CHECK(!strstr(text, "install-aaaa.id") && strstr(text, "install-bbbb.id"));
	CHECK(exists("mnt-sda3/lineage2/system.img") && exists("efivars/Boot0000-" EFI_GLOBAL_GUID));
	read_rel("mnt-sda1/EFI/BOOT/BOOTX64.EFI", text, sizeof text);
	CHECK_STR(text, "WINDOWS");
	fake_teardown();
}

static void uninstalling_keeps_the_users_own_files(void)
{
	setup_machine();
	CHECK_STR(install("image", "folder", 0), "ok");
	fake_sh("printf mine > %s", "mnt-sda3/lineage/notes.txt");
	CHECK_STR(uninstall("lineage", 1), "ok");
	CHECK(exists("mnt-sda3/lineage/notes.txt") && !exists("mnt-sda3/lineage/system.img"));
	CHECK(!exists("mnt-sda3/lineage/userdata") && !exists("mnt-sda3/lineage/kernel"));
	fake_teardown();
}

static void uninstalling_the_last_bios_install_puts_the_old_boot_code_back(void)
{
	setup_machine();
	unsigned char before[512], after[512];
	read_sector(0, before);
	CHECK_STR(install_as("lineage", "aaaa", "image", "folder", 0), "ok");
	CHECK_STR(uninstall("lineage", 0), "ok");
	read_sector(0, after);
	CHECK(!memcmp(before, after, 512));
	CHECK(!exists("mnt-sda3/pc-grub"));
	fake_teardown();
}

static void uninstalling_nothing_says_so(void)
{
	setup_machine();
	CHECK_STR(uninstall("lineage", 1), "not_installed");
	fake_teardown();
}

int main(void)
{
	RUN(every_install_is_in_the_one_menu);
	RUN(the_menu_offers_the_other_systems);
	RUN(a_bios_computer_gets_grub_in_the_disk_gap);
	RUN(uninstalling_removes_the_install_its_entry_and_our_boot_loader);
	RUN(uninstalling_one_install_keeps_the_others);
	RUN(uninstalling_keeps_the_users_own_files);
	RUN(uninstalling_the_last_bios_install_puts_the_old_boot_code_back);
	RUN(uninstalling_nothing_says_so);
	RUN(installs_images_and_the_boot_entry);
	RUN(extracts_the_system_and_makes_a_data_folder);
	RUN(keeps_an_existing_boot_loader_at_the_fallback_path);
	RUN(a_reinstall_moves_the_boot_entry_from_grub_to_shim);
	RUN(our_fallback_loader_is_brought_up_to_date);
	RUN(another_systems_shim_at_the_fallback_path_stays);
	RUN(an_update_keeps_the_previous_system_and_the_data);
	RUN(an_update_can_change_the_system_form);
	RUN(a_failed_copy_puts_the_old_system_back);
	RUN(reads_what_an_install_folder_holds);
	return CHECK_DONE();
}
