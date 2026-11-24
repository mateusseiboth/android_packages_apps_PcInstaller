#include <sys/stat.h>

#include "check.h"
#include "disks.h"
#include "fakesys.h"
#include "install.h"
#include "util.h"

static struct live_source source;
static struct partition target, esp;
static int last_percent, went_back;

static void setup_machine(void)
{
	fake_setup();
	fake_disk("sda", 512);
	fake_partition("sda", "sda1", 1, 65536);
	fake_partition("sda", "sda3", 3, 1048576);
	fake_sh("truncate -s 64M dev/block/%s && sgdisk -o -n 1:2048:+32M -t 1:ef00 -n 3:0:0 -t 3:8300 "
		"dev/block/sda >/dev/null", "sda");
	fake_partition("sda", "loop9", 0, 0);
	fake_sh("mkdir -p mnt-sdb1/%1$s && printf KERNEL > mnt-sdb1/%1$s/kernel && printf INITRD > mnt-sdb1/%1$s/initrd.img"
		" && printf GRUB > mnt-sdb1/%1$s/grubx64.efi && truncate -s 32M mnt-sdb1/%1$s/system.img"
		" && printf NEW | dd conv=notrunc of=mnt-sdb1/%1$s/system.img 2>/dev/null", "lineage");
	/* What the loop-mounted system image holds. */
	fake_sh("mkdir -p mnt-loop9/system/bin && printf init > mnt-loop9/system/bin/init && ln -s /system/bin/init %s",
		"mnt-loop9/init");
	source = (struct live_source){ .name = "sdb1", .disk = "sdb", .folder = "lineage",
				       .sizes = { 6, 6, 32 << 20, 4 } };
	snprintf(source.dir, sizeof source.dir, "%s/mnt-sdb1/lineage", fake_root);
	target = (struct partition){ .name = "sda3", .disk = "sda", .fs = "ext4", .number = 3, .free = 1ULL << 40 };
	esp = (struct partition){ .name = "sda1", .disk = "sda", .fs = "vfat", .number = 1, .is_esp = 1 };
	last_percent = -1;
	went_back = 0;
}

static void on_progress(void *ctx, const char *step, int percent)
{
	(void)ctx;
	(void)step;
	went_back |= percent < last_percent;
	last_percent = percent;
}

static const char *install(const char *system, const char *data, unsigned long long data_size)
{
	struct install_job job = { &fake_env, &FAKE_OPS, &source, &target, &esp,
				   { .part = "sda3", .folder = "lineage", .system_form = system, .data_form = data,
				     .data_size = data_size },
				   "0011223344556677" };
	const char *error = NULL;
	return run_install(&job, on_progress, NULL, &error) ? error : "ok";
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

	read_rel("mnt-sda1/EFI/LineageOS/grubx64.efi", text, sizeof text);
	CHECK_STR(text, "GRUB");
	read_rel("mnt-sda1/EFI/LineageOS/grub.cfg", text, sizeof text);
	CHECK(strstr(text, "--file /lineage/install-0011223344556677.id") != NULL);
	CHECK(!strstr(text, "pc.previous=1"));
	CHECK(exists("mnt-sda1/EFI/BOOT/BOOTX64.EFI"));
	CHECK(exists("efivars/Boot0000-" EFI_GLOBAL_GUID));
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
	read_install_state(fake_path("mnt-sda3/lineage"), &s);
	CHECK(s.has_system && s.has_data && s.old_system_bytes == 1 << 20);
	fake_teardown();
}

int main(void)
{
	RUN(installs_images_and_the_boot_entry);
	RUN(extracts_the_system_and_makes_a_data_folder);
	RUN(keeps_an_existing_boot_loader_at_the_fallback_path);
	RUN(an_update_keeps_the_previous_system_and_the_data);
	RUN(an_update_can_change_the_system_form);
	RUN(a_failed_copy_puts_the_old_system_back);
	RUN(reads_what_an_install_folder_holds);
	return CHECK_DONE();
}
