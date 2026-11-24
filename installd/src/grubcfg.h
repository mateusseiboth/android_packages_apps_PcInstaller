/*
 * The kernel command line and the GRUB menu of an installed system. The base arguments match
 * kernel_cmdline() in device/pc/pc_x86_64/iso/build_iso.py (tests/test_cmdline_matches_iso.py).
 */
#pragma once
#include <stddef.h>

struct grub_params {
	const char *folder;
	const char *id;    /* the install's id: GRUB and pc-init find the folder by <folder>/install-<id>.id */
	int has_previous;  /* system.old kept by an update */
};

void build_kernel_cmdline(char *out, size_t cap, const char *folder, const char *extra);
void build_id_file(char *out, size_t cap, const char *folder, const char *id);
/* 0 on success, -1 when cap is too small. */
int build_grub_cfg(char *out, size_t cap, const struct grub_params *p);
