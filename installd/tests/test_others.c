#include <stdlib.h>

#include "check.h"
#include "others.h"
#include "treecopy.h"

static char dir[64];

static void setup(const char *files)
{
	snprintf(dir, sizeof dir, "/tmp/others-XXXXXX");
	CHECK(mkdtemp(dir) != NULL);
	char cmd[1024];
	snprintf(cmd, sizeof cmd, "cd %s && for f in %s; do mkdir -p \"$(dirname $f)\" && touch $f; done", dir, files);
	CHECK(!system(cmd));
}

static void teardown(void) { remove_tree(dir); }

static const struct other_system *find_title(const struct other_system *list, int n, const char *title)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(list[i].title, title))
			return &list[i];
	return NULL;
}

static void finds_windows_and_other_linux_on_an_esp(void)
{
	setup("EFI/Microsoft/Boot/bootmgfw.efi EFI/fedora/shimx64.efi EFI/fedora/grubx64.efi EFI/ubuntu/grubx64.efi "
	      "EFI/systemd/systemd-bootx64.efi EFI/LineageOS/grubx64.efi EFI/BOOT/BOOTX64.EFI EFI/Dell/readme.txt");
	struct other_system list[MAX_OTHERS];
	int n = find_efi_systems(dir, "1234-abcd", list, MAX_OTHERS);
	CHECK(n == 4);
	const struct other_system *w = find_title(list, n, "Windows Boot Manager");
	CHECK(w && !strcmp(w->loader, "/EFI/Microsoft/Boot/bootmgfw.efi") && w->kind == OTHER_EFI);
	CHECK(w && !strcmp(w->uuid, "1234-abcd"));
	const struct other_system *f = find_title(list, n, "Fedora");
	CHECK(f && !strcmp(f->loader, "/EFI/fedora/shimx64.efi"));
	CHECK(find_title(list, n, "Ubuntu") != NULL);
	const struct other_system *s = find_title(list, n, "Linux Boot Manager");
	CHECK(s && !strcmp(s->loader, "/EFI/systemd/systemd-bootx64.efi"));
	teardown();
}

static void matches_names_in_any_case(void)
{
	setup("efi/microsoft/boot/BOOTMGFW.EFI");
	struct other_system list[MAX_OTHERS];
	int n = find_efi_systems(dir, "1234-abcd", list, MAX_OTHERS);
	CHECK(n == 1 && !strcmp(list[0].loader, "/efi/microsoft/boot/BOOTMGFW.EFI"));
	teardown();
}

static void finds_what_a_bios_partition_can_start(void)
{
	struct other_system list[MAX_OTHERS];
	setup("bootmgr Windows/explorer.exe");
	CHECK(find_bios_systems(dir, "0102030405060708", "sda1", list, MAX_OTHERS) == 1);
	CHECK_STR(list[0].title, "Windows (sda1)");
	CHECK(list[0].kind == OTHER_BOOT_SECTOR);
	teardown();

	setup("boot/grub2/grub.cfg etc/os-release");
	CHECK(find_bios_systems(dir, "0f5a7f3c-1111-2222-3333-444455556666", "fedora", list, MAX_OTHERS) == 1);
	CHECK_STR(list[0].title, "Linux (fedora)");
	CHECK_STR(list[0].loader, "/boot/grub2/grub.cfg");
	CHECK(list[0].kind == OTHER_CONFIGFILE);
	teardown();

	setup("grub/grub.cfg");
	CHECK(find_bios_systems(dir, "0f5a7f3c-1111-2222-3333-444455556666", "boot", list, MAX_OTHERS) == 1);
	CHECK_STR(list[0].loader, "/grub/grub.cfg");
	teardown();

	setup("home/user/notes.txt");
	CHECK(find_bios_systems(dir, "0f5a", "data", list, MAX_OTHERS) == 0);
	teardown();
}

static void without_a_uuid_grub_cannot_find_it(void)
{
	struct other_system list[MAX_OTHERS];
	setup("bootmgr");
	CHECK(find_bios_systems(dir, "", "sda1", list, MAX_OTHERS) == 0);
	teardown();
}

int main(void)
{
	RUN(finds_windows_and_other_linux_on_an_esp);
	RUN(matches_names_in_any_case);
	RUN(finds_what_a_bios_partition_can_start);
	RUN(without_a_uuid_grub_cannot_find_it);
	return CHECK_DONE();
}
