/*
 * The live source: the partition the live ISO booted from. pc-init loop-mounts <folder>/system.img
 * from it as Android's root and hides its own mount of the partition, so pc-installd follows the
 * loop device behind / to that partition and mounts it again, read-only.
 */
#pragma once
#include <limits.h>

#include "model.h"
#include "sysops.h"

struct live_source {
	char name[32];   /* sdb1, or sr0 / sdb for an ISO without partitions */
	char disk[32];
	char folder[128];
	char dir[PATH_MAX]; /* <work>/src/<folder> once mounted */
	struct source_sizes sizes;
};

int parse_root_device(const char *mountinfo, unsigned *major, unsigned *minor);
/* 1 when pc.live=1; folder gets pc.src= (default lineage). */
int parse_live_cmdline(const char *cmdline, char *folder, size_t cap);
int find_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *out);
/* Mounts the source and measures kernel, initrd.img, system.img and grubx64.efi; -1 if one is missing. */
int mount_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *s);
