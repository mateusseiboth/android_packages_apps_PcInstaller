/*
 * A menu directory (EFI/LineageOS on an ESP, or pc-grub on a partition for BIOS): one entry file per
 * install in entries/<id>.cfg, and grub.cfg rebuilt from all of them. Every install registers its
 * entry in every menu directory on the computer, so whichever GRUB starts lists every install.
 */
#pragma once

int save_menu_entry(const char *dir, const char *id, const char *entry);
/* A missing entry is not an error. */
int remove_menu_entry(const char *dir, const char *id);
int count_menu_entries(const char *dir);
/* default_id falls back to the first entry when it has none. */
int rebuild_menu(const char *dir, const char *others, const char *default_id, int is_efi);
