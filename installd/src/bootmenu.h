/*
 * The computer's GRUB menus. A scan finds every menu directory we keep (EFI/LineageOS on an ESP,
 * pc-grub on a partition for BIOS) and every other system GRUB can start; an install adds its entry
 * to all of them, a removal takes it out and deletes a menu left without installs.
 */
#pragma once
#include "model.h"
#include "others.h"
#include "sysops.h"

#define MAX_MENU_PLACES 16

struct menu_place {
	const struct partition *part;
	int is_efi;
};

struct machine_scan {
	struct menu_place places[MAX_MENU_PLACES];
	int place_count;
	struct other_system efi[MAX_OTHERS];
	int efi_count;
	struct other_system bios[MAX_OTHERS];
	int bios_count;
};

/* The menu directory inside a partition: EFI/LineageOS or pc-grub. */
const char *find_menu_dir(int is_efi);
void scan_machine(const struct sys_env *env, const struct sys_ops *ops, const struct partition *parts, int count,
		  struct machine_scan *out);
void add_menu_place(struct machine_scan *s, const struct partition *part, int is_efi);
int apply_menu_entry(const struct sys_env *env, const struct sys_ops *ops, const struct machine_scan *s,
		     const char *id, const char *entry);
/* A menu left without installs is removed, with the GRUB we put next to it. */
int drop_menu_entry(const struct sys_env *env, const struct sys_ops *ops, const struct machine_scan *s,
		    const char *id);
int count_efi_menus(const struct machine_scan *s);
