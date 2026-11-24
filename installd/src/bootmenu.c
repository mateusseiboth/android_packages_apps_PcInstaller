#include "bootmenu.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "biosboot.h"
#include "efifiles.h"
#include "menus.h"
#include "plan.h"
#include "treecopy.h"
#include "util.h"

#define OTHERS_MAX (MAX_OTHERS * 640)

static const char *const MENU_DIRS[] = { "pc-grub", EFI_MENU_DIR };

const char *find_menu_dir(int is_efi) { return MENU_DIRS[is_efi != 0]; }

/* ---- mounting ------------------------------------------------------------------------- */

static void build_mount_dir(char *out, size_t cap, const struct sys_env *env, const char *name)
{
	snprintf(out, cap, "%s/%s", env->work, name);
	make_dirs(out, 0700);
}

static int mount_part(const struct sys_env *env, const struct sys_ops *ops, const struct partition *p,
		      const char *dir, int read_only)
{
	char dev[PATH_MAX];
	snprintf(dev, sizeof dev, "%s/%s", env->dev_dir, p->name);
	return ops->mount(env, dev, dir, find_mount_type(p->fs), read_only);
}

static const char *find_place_name(const struct partition *p) { return *p->label ? p->label : p->name; }

/* ---- scanning ------------------------------------------------------------------------- */

static int is_scannable(const struct partition *p)
{
	return !p->is_source && !p->cannot_mount && (p->is_esp || !find_fs_rule(p->fs)->reason);
}

static int has_menu(const char *mounted, int is_efi)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", mounted, find_menu_dir(is_efi));
	return is_dir(path);
}

static void scan_esp(const char *mounted, const struct partition *p, struct machine_scan *s)
{
	if (has_menu(mounted, 1))
		add_menu_place(s, p, 1);
	s->efi_count += find_efi_systems(mounted, p->uuid, s->efi + s->efi_count, MAX_OTHERS - s->efi_count);
}

static void scan_partition(const char *mounted, const struct partition *p, struct machine_scan *s)
{
	if (has_menu(mounted, 0))
		add_menu_place(s, p, 0);
	s->bios_count += find_bios_systems(mounted, p->uuid, find_place_name(p), s->bios + s->bios_count,
					   MAX_OTHERS - s->bios_count);
}

void scan_machine(const struct sys_env *env, const struct sys_ops *ops, const struct partition *parts, int count,
		  struct machine_scan *out)
{
	static void (*const SCANS[])(const char *, const struct partition *, struct machine_scan *) = {
		scan_partition, scan_esp
	};
	char dir[PATH_MAX];
	*out = (struct machine_scan){ 0 };
	build_mount_dir(dir, sizeof dir, env, "probe");
	for (int i = 0; i < count; i++) {
		if (!is_scannable(&parts[i]) || mount_part(env, ops, &parts[i], dir, 1))
			continue;
		SCANS[parts[i].is_esp != 0](dir, &parts[i], out);
		ops->umount(env, dir);
	}
}

void add_menu_place(struct machine_scan *s, const struct partition *part, int is_efi)
{
	for (int i = 0; i < s->place_count; i++)
		if (!strcmp(s->places[i].part->name, part->name) && s->places[i].is_efi == is_efi)
			return;
	if (s->place_count < MAX_MENU_PLACES)
		s->places[s->place_count++] = (struct menu_place){ part, is_efi };
}

/* ---- writing every menu --------------------------------------------------------------- */

static void build_others(const struct machine_scan *s, int is_efi, char *out, size_t cap)
{
	const struct other_system *list = is_efi ? s->efi : s->bios;
	int count = is_efi ? s->efi_count : s->bios_count;
	size_t used = 0;
	out[0] = 0;
	for (int i = 0; i < count && used < cap; i++) {
		build_other_entry(out + used, cap - used, &list[i]);
		used += strlen(out + used);
	}
}

typedef int (*place_action)(const struct sys_env *env, const struct menu_place *place, const char *dir,
			    const char *others, const char *id, const char *entry);

static int run_in_places(const struct sys_env *env, const struct sys_ops *ops, const struct machine_scan *s,
			 place_action action, const char *id, const char *entry)
{
	char mounted[PATH_MAX], dir[PATH_MAX * 2], *others = malloc(OTHERS_MAX);
	int failed = !others;
	build_mount_dir(mounted, sizeof mounted, env, "menu");
	for (int i = 0; !failed && i < s->place_count; i++) {
		const struct menu_place *place = &s->places[i];
		if (mount_part(env, ops, place->part, mounted, 0)) {
			failed = 1;
			break;
		}
		snprintf(dir, sizeof dir, "%s/%s", mounted, find_menu_dir(place->is_efi));
		build_others(s, place->is_efi, others, OTHERS_MAX);
		failed = action(env, place, dir, others, id, entry);
		ops->umount(env, mounted);
	}
	free(others);
	return failed ? -1 : 0;
}

static int save_entry_in(const struct sys_env *env, const struct menu_place *place, const char *dir,
			 const char *others, const char *id, const char *entry)
{
	(void)env;
	return make_dirs(dir, 0755) || save_menu_entry(dir, id, entry) || rebuild_menu(dir, others, id, place->is_efi);
}

int apply_menu_entry(const struct sys_env *env, const struct sys_ops *ops, const struct machine_scan *s,
		     const char *id, const char *entry)
{
	return run_in_places(env, ops, s, save_entry_in, id, entry);
}

/* ---- removing ------------------------------------------------------------------------- */

/* The fallback loader goes only when it is ours. */
static int remove_efi_menu(const struct sys_env *env, const struct menu_place *place, const char *dir)
{
	(void)env;
	(void)place;
	char esp[PATH_MAX];
	snprintf(esp, sizeof esp, "%.*s", (int)(strlen(dir) - strlen("/" EFI_MENU_DIR)), dir);
	if (is_our_fallback(esp))
		remove_fallback_files(esp);
	return remove_tree(dir);
}

/* The disk gets back the boot code it had before GRUB. */
static int remove_bios_menu(const struct sys_env *env, const struct menu_place *place, const char *dir)
{
	char backup[PATH_MAX], disk[PATH_MAX];
	snprintf(backup, sizeof backup, "%s/mbr-%s.bak", dir, place->part->disk);
	snprintf(disk, sizeof disk, "%s/%s", env->dev_dir, place->part->disk);
	if (is_file(backup) && restore_bios_boot(disk, backup))
		return -1;
	return remove_tree(dir);
}

static int drop_entry_in(const struct sys_env *env, const struct menu_place *place, const char *dir,
			 const char *others, const char *id, const char *entry)
{
	static int (*const REMOVERS[])(const struct sys_env *, const struct menu_place *, const char *) = {
		remove_bios_menu, remove_efi_menu
	};
	(void)entry;
	if (remove_menu_entry(dir, id))
		return -1;
	if (!count_menu_entries(dir))
		return REMOVERS[place->is_efi != 0](env, place, dir);
	return rebuild_menu(dir, others, NULL, place->is_efi);
}

int drop_menu_entry(const struct sys_env *env, const struct sys_ops *ops, const struct machine_scan *s,
		    const char *id)
{
	return run_in_places(env, ops, s, drop_entry_in, id, NULL);
}

int count_efi_menus(const struct machine_scan *s)
{
	int n = 0;
	for (int i = 0; i < s->place_count; i++)
		n += s->places[i].is_efi != 0;
	return n;
}
