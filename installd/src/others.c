#include "others.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* ---- names in any case (FAT keeps whatever case the other system wrote) ---------------- */

static int find_child(const char *dir, const char *name, char *out, size_t cap)
{
	DIR *d = opendir(dir);
	int found = -1;
	for (struct dirent *e; d && found && (e = readdir(d));)
		if (!strcasecmp(e->d_name, name)) {
			snprintf(out, cap, "%s", e->d_name);
			found = 0;
		}
	if (d)
		closedir(d);
	return found;
}

/* rel: a/b/c, matched component by component; out gets the path as stored, from "/". */
static int find_path(const char *base, const char *rel, char *out, size_t cap)
{
	char parts[256], dir[PATH_MAX], name[256];
	snprintf(parts, sizeof parts, "%s", rel);
	snprintf(dir, sizeof dir, "%s", base);
	out[0] = 0;
	for (char *save = NULL, *p = strtok_r(parts, "/", &save); p; p = strtok_r(NULL, "/", &save)) {
		if (find_child(dir, p, name, sizeof name))
			return -1;
		size_t used = strlen(out);
		snprintf(out + used, cap - used, "/%s", name);
		snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/%s", name);
	}
	return 0;
}

static int add_other(struct other_system *out, int count, int max, const char *title, const char *uuid,
		     const char *loader, enum other_kind kind)
{
	if (count >= max)
		return count;
	struct other_system *o = &out[count];
	*o = (struct other_system){ .kind = kind };
	snprintf(o->title, sizeof o->title, "%s", title);
	snprintf(o->uuid, sizeof o->uuid, "%s", uuid);
	snprintf(o->loader, sizeof o->loader, "%s", loader);
	return count + 1;
}

/* ---- EFI ------------------------------------------------------------------------------ */

/* Ours, the fallback loader and Windows (found by its own rule) are not "other Linux". */
static const char *const SKIPPED_DIRS[] = { "LineageOS", "BOOT", "Microsoft" };
/* shim first: it starts the distribution's signed GRUB under Secure Boot. */
static const char *const LINUX_LOADERS[] = { "shimx64.efi", "grubx64.efi", "systemd-bootx64.efi" };
static const struct {
	const char *dir;
	const char *title;
} KNOWN_TITLES[] = { { "systemd", "Linux Boot Manager" } };

static int is_skipped_dir(const char *name)
{
	for (size_t i = 0; i < sizeof SKIPPED_DIRS / sizeof *SKIPPED_DIRS; i++)
		if (!strcasecmp(name, SKIPPED_DIRS[i]))
			return 1;
	return name[0] == '.';
}

static void build_efi_title(const char *dir, char *out, size_t cap)
{
	for (size_t i = 0; i < sizeof KNOWN_TITLES / sizeof *KNOWN_TITLES; i++)
		if (!strcasecmp(dir, KNOWN_TITLES[i].dir)) {
			snprintf(out, cap, "%s", KNOWN_TITLES[i].title);
			return;
		}
	snprintf(out, cap, "%.63s", dir);
	out[0] = (char)toupper((unsigned char)out[0]);
}

static int add_linux_loader(const char *esp_dir, const char *efi, const char *dir, const char *uuid,
			    struct other_system *out, int count, int max)
{
	char rel[512], loader[PATH_MAX], title[96];
	for (size_t i = 0; i < sizeof LINUX_LOADERS / sizeof *LINUX_LOADERS; i++) {
		snprintf(rel, sizeof rel, "%s/%s/%s", efi, dir, LINUX_LOADERS[i]);
		if (find_path(esp_dir, rel, loader, sizeof loader))
			continue;
		build_efi_title(dir, title, sizeof title);
		return add_other(out, count, max, title, uuid, loader, OTHER_EFI);
	}
	return count;
}

int find_efi_systems(const char *esp_dir, const char *uuid, struct other_system *out, int max)
{
	char loader[PATH_MAX], efi[64], path[PATH_MAX];
	int count = 0;
	if (find_path(esp_dir, "EFI", efi, sizeof efi))
		return 0;
	if (!find_path(esp_dir, "EFI/Microsoft/Boot/bootmgfw.efi", loader, sizeof loader))
		count = add_other(out, count, max, "Windows Boot Manager", uuid, loader, OTHER_EFI);
	snprintf(path, sizeof path, "%s%s", esp_dir, efi);
	DIR *d = opendir(path);
	for (struct dirent *e; d && (e = readdir(d));)
		if (!is_skipped_dir(e->d_name))
			count = add_linux_loader(esp_dir, efi + 1, e->d_name, uuid, out, count, max);
	if (d)
		closedir(d);
	return count;
}

/* ---- BIOS ----------------------------------------------------------------------------- */

static const struct {
	const char *path;
	const char *title;
	enum other_kind kind;
} BIOS_RULES[] = {
	{ "bootmgr", "Windows (%s)", OTHER_BOOT_SECTOR },
	{ "ntldr", "Windows (%s)", OTHER_BOOT_SECTOR },
	{ "boot/grub2/grub.cfg", "Linux (%s)", OTHER_CONFIGFILE },
	{ "boot/grub/grub.cfg", "Linux (%s)", OTHER_CONFIGFILE },
	{ "grub2/grub.cfg", "Linux (%s)", OTHER_CONFIGFILE },
	{ "grub/grub.cfg", "Linux (%s)", OTHER_CONFIGFILE },
};

int find_bios_systems(const char *dir, const char *uuid, const char *place, struct other_system *out, int max)
{
	char found[PATH_MAX], title[96];
	if (!*uuid)
		return 0;
	for (size_t i = 0; i < sizeof BIOS_RULES / sizeof *BIOS_RULES; i++) {
		if (find_path(dir, BIOS_RULES[i].path, found, sizeof found))
			continue;
		snprintf(title, sizeof title, BIOS_RULES[i].title, place);
		return add_other(out, 0, max, title, uuid, found, BIOS_RULES[i].kind);
	}
	return 0;
}
