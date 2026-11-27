/*
 * The GRUB menu. Every install owns one entry block (finding its folder by its id file); a menu
 * directory keeps one block per install and grub.cfg is rebuilt from all of them plus the other
 * systems found on the computer. The base kernel arguments match kernel_cmdline() in
 * device/pc/pc_x86_64/iso/build_iso.py (tests/test_daemon.py).
 */
#pragma once
#include <stddef.h>

struct entry_params {
	const char *folder;
	const char *id;    /* GRUB and pc-init find the folder by <folder>/install-<id>.id */
	const char *title; /* where the install lives: partition label or name */
	int previous;      /* PREVIOUS_* flags: what an update or a kernel patch kept */
};

#define PREVIOUS_SYSTEM 1 /* system.old (or a staged update that makes the system system.old) */
#define PREVIOUS_KERNEL 2 /* kernel.old: the previous system starts with its own kernel */
#define PREVIOUS_INITRD 4 /* initrd.old.img */

enum other_kind {
	OTHER_EFI,         /* chainload an EFI loader on an ESP */
	OTHER_BOOT_SECTOR, /* BIOS: chainload a partition's boot sector (Windows) */
	OTHER_CONFIGFILE,  /* BIOS: read another Linux's GRUB menu */
};

struct other_system {
	char title[96];
	char uuid[40];    /* file system holding the loader, for search --fs-uuid */
	char loader[160]; /* path of the loader or menu on it */
	enum other_kind kind;
};

struct menu_params {
	const char *const *entries; /* install entry blocks, in menu order */
	int count;
	const char *others;         /* entry blocks of the other systems */
	const char *default_id;     /* install whose entry starts by default */
	int is_efi;
};

void build_kernel_cmdline(char *out, size_t cap, const char *folder, const char *extra);
void build_id_file(char *out, size_t cap, const char *folder, const char *id);
/* Each returns 0, or -1 when cap is too small. */
int build_install_entry(char *out, size_t cap, const struct entry_params *p);
int build_other_entry(char *out, size_t cap, const struct other_system *o);
int build_menu(char *out, size_t cap, const struct menu_params *m);
