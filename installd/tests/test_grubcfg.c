#include "check.h"
#include "grubcfg.h"

static int count(const char *haystack, const char *needle)
{
	int n = 0;
	for (const char *p = strstr(haystack, needle); p; p = strstr(p + 1, needle))
		n++;
	return n;
}

static void cmdline_boots_android_from_the_folder(void)
{
	char out[1024];
	build_kernel_cmdline(out, sizeof out, "lineage", "quiet");
	CHECK_STR(out, "androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware "
		       "console=tty0 loglevel=4 pc.src=lineage quiet");
	build_kernel_cmdline(out, sizeof out, "os/lineage", "");
	CHECK_STR(out, "androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware "
		       "console=tty0 loglevel=4 pc.src=os/lineage");
}

static void normal_and_safe_graphics_entries(void)
{
	char cfg[4096];
	struct grub_params p = { .folder = "lineage", .id = "0123abcd" };
	CHECK(!build_grub_cfg(cfg, sizeof cfg, &p));
	CHECK(count(cfg, "menuentry ") == 2);
	CHECK(count(cfg, "linux /lineage/kernel ") == 2);
	CHECK(count(cfg, "initrd /lineage/initrd.img") == 2);
	CHECK(count(cfg, "pc.src=lineage pc.id=0123abcd") == 2);
	CHECK(count(cfg, " nomodeset") == 1);
	CHECK(!strstr(cfg, "pc.previous=1"));
	CHECK(!strstr(cfg, "pc.live"));
}

static void finds_the_install_by_its_id_file(void)
{
	char cfg[4096];
	struct grub_params p = { .folder = "os/lineage", .id = "feedf00d" };
	build_grub_cfg(cfg, sizeof cfg, &p);
	CHECK(strstr(cfg, "search --no-floppy --set=root --file /os/lineage/install-feedf00d.id\n") != NULL);
}

static void offers_the_previous_system_after_an_update(void)
{
	char cfg[4096];
	struct grub_params p = { .folder = "lineage", .id = "0123abcd", .has_previous = 1 };
	build_grub_cfg(cfg, sizeof cfg, &p);
	CHECK(count(cfg, "menuentry ") == 3);
	CHECK(count(cfg, "pc.previous=1") == 1);
}

static void the_id_file_name_is_shared(void)
{
	char name[128];
	build_id_file(name, sizeof name, "lineage", "0123abcd");
	CHECK_STR(name, "lineage/install-0123abcd.id");
}

static void a_small_buffer_fails_instead_of_truncating(void)
{
	char cfg[64];
	struct grub_params p = { .folder = "lineage", .id = "0123abcd" };
	CHECK(build_grub_cfg(cfg, sizeof cfg, &p));
}

int main(void)
{
	RUN(cmdline_boots_android_from_the_folder);
	RUN(normal_and_safe_graphics_entries);
	RUN(finds_the_install_by_its_id_file);
	RUN(offers_the_previous_system_after_an_update);
	RUN(the_id_file_name_is_shared);
	RUN(a_small_buffer_fails_instead_of_truncating);
	return CHECK_DONE();
}
