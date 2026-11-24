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

static void the_id_file_name_is_shared(void)
{
	char name[128];
	build_id_file(name, sizeof name, "lineage", "0123abcd");
	CHECK_STR(name, "lineage/install-0123abcd.id");
}

static void an_install_entry_finds_its_own_folder(void)
{
	char e[4096];
	struct entry_params p = { "lineage", "0123abcd", "Dados", 0 };
	CHECK(!build_install_entry(e, sizeof e, &p));
	CHECK(strstr(e, "menuentry \"LineageOS (Dados)\" --id lineage-0123abcd {\n") == e);
	CHECK(count(e, "search --no-floppy --set=root --file /lineage/install-0123abcd.id\n") == 2);
	CHECK(count(e, "linux /lineage/kernel ") == 2);
	CHECK(count(e, "initrd /lineage/initrd.img") == 2);
	CHECK(count(e, "pc.src=lineage pc.id=0123abcd") == 2);
	CHECK(count(e, " nomodeset") == 1);
	CHECK(strstr(e, "submenu \"LineageOS (Dados): more options\"") != NULL);
	CHECK(!strstr(e, "pc.previous=1"));
	CHECK(!strstr(e, "pc.live"));
}

static void offers_the_previous_system_after_an_update(void)
{
	char e[4096];
	struct entry_params p = { "lineage", "0123abcd", "Dados", 1 };
	build_install_entry(e, sizeof e, &p);
	CHECK(count(e, "menuentry ") == 3);
	CHECK(count(e, "pc.previous=1") == 1);
}

static void titles_cannot_break_the_menu(void)
{
	char e[4096];
	struct entry_params p = { "lineage", "0123abcd", "My \"disk\" $x \\", 0 };
	build_install_entry(e, sizeof e, &p);
	CHECK(strstr(e, "menuentry \"LineageOS (My _disk_ _x _)\"") != NULL);
}

static void other_systems_boot_through_their_own_loaders(void)
{
	char e[1024];
	struct other_system windows = { "Windows Boot Manager", "1234-abcd", "/EFI/Microsoft/Boot/bootmgfw.efi", OTHER_EFI };
	CHECK(!build_other_entry(e, sizeof e, &windows));
	CHECK_STR(e, "menuentry \"Windows Boot Manager\" {\n"
		     "    search --no-floppy --set=root --fs-uuid 1234-abcd\n"
		     "    chainloader /EFI/Microsoft/Boot/bootmgfw.efi\n}\n\n");
	struct other_system bios = { "Windows (sda1)", "0102030405060708", "", OTHER_BOOT_SECTOR };
	build_other_entry(e, sizeof e, &bios);
	CHECK(strstr(e, "    chainloader +1\n") != NULL);
	struct other_system other_linux = { "Linux (fedora)", "0f5a7f3c-1111-2222-3333-444455556666", "/boot/grub2/grub.cfg",
				      OTHER_CONFIGFILE };
	build_other_entry(e, sizeof e, &other_linux);
	CHECK(strstr(e, "    configfile /boot/grub2/grub.cfg\n") != NULL);
}

static void the_menu_lists_every_install_and_the_other_systems(void)
{
	char menu[8192];
	const char *entries[] = { "menuentry \"LineageOS (A)\" --id lineage-aa {\n}\n",
				  "menuentry \"LineageOS (B)\" --id lineage-bb {\n}\n" };
	struct menu_params m = { entries, 2, "menuentry \"Windows Boot Manager\" {\n}\n", "bb", 1 };
	CHECK(!build_menu(menu, sizeof menu, &m));
	CHECK(strstr(menu, "set default=lineage-bb\n") != NULL);
	CHECK(strstr(menu, "set timeout=5\n") != NULL);
	const char *a = strstr(menu, "LineageOS (A)"), *b = strstr(menu, "LineageOS (B)"), *w = strstr(menu, "Windows");
	CHECK(a && b && w && a < b && b < w);
	CHECK(strstr(menu, "fwsetup") != NULL);
	m.is_efi = 0;
	build_menu(menu, sizeof menu, &m);
	CHECK(!strstr(menu, "fwsetup"));
}

static void a_small_buffer_fails_instead_of_truncating(void)
{
	char e[64];
	struct entry_params p = { "lineage", "0123abcd", "Dados", 0 };
	CHECK(build_install_entry(e, sizeof e, &p));
	const char *entries[] = { "menuentry \"LineageOS (A)\" --id lineage-aa {\n}\n" };
	struct menu_params m = { entries, 1, "", "aa", 0 };
	CHECK(build_menu(e, sizeof e, &m));
}

int main(void)
{
	RUN(cmdline_boots_android_from_the_folder);
	RUN(the_id_file_name_is_shared);
	RUN(an_install_entry_finds_its_own_folder);
	RUN(offers_the_previous_system_after_an_update);
	RUN(titles_cannot_break_the_menu);
	RUN(other_systems_boot_through_their_own_loaders);
	RUN(the_menu_lists_every_install_and_the_other_systems);
	RUN(a_small_buffer_fails_instead_of_truncating);
	return CHECK_DONE();
}
