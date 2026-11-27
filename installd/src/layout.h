/*
 * The install folder beyond the system and the data: what updates and patches leave there
 * (device/pc/pc_x86_64/docs/UPDATES.md). Every final name is reached by a rename of a complete file.
 */
#pragma once

#define KERNEL_FILE "kernel"
#define KERNEL_OLD "kernel.old"
#define INITRD_FILE "initrd.img"
#define INITRD_OLD "initrd.old.img"
#define SYSTEM_NEW_IMAGE "system.new.img"
#define SYSTEM_NEW_DIR "system.new"
#define PATCHES_DIR "patches"
#define UPDATE_LOCALE "update-locale"

/* PREVIOUS_* flags (grubcfg.h) from what the folder at dir holds: a previous or staged system, and the
 * kernel and initrd from before the last update or kernel patch. */
int read_previous_flags(const char *dir);
/* dir/name.tmp is complete and takes name's place; the file it replaces becomes old_name, or is
 * dropped when old_name is NULL. */
int swap_in_file(const char *dir, const char *name, const char *old_name);
/* dir/old_name becomes name again, when it exists. */
int restore_old_file(const char *dir, const char *name, const char *old_name);
/* An update waiting for the next boot (system.new*, update-locale) and what a cut-off write left
 * (*.tmp). */
int remove_staged_update(const char *dir);
int remove_patches(const char *dir);
