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

/* Which build a system is (build.prop). */
struct build_info {
	unsigned long long utc; /* ro.build.date.utc */
	char version[96];       /* ro.lineage.display.version, else ro.build.display.id */
	char build[24];         /* ro.system.build.date.utc: the build updates and patches name */
};

/* The install an installed system was started from (pc.src, pc.id, and the partition behind /). */
struct running_install {
	char part[32];
	char folder[128];
	char id[64];
	struct build_info build;
};

/* A live USB drive holds <folder>/install-live.id next to the system. */
#define LIVE_FOLDER "lineage"

void parse_build_prop(const char *text, struct build_info *out);
int parse_root_device(const char *mountinfo, unsigned *major, unsigned *minor);
/* 1 when pc.live=1; folder gets pc.src= (default lineage). */
int parse_live_cmdline(const char *cmdline, char *folder, size_t cap);
int find_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *out);
/* Mounts the source and measures what an install copies (kernel, initrd.img, system.img, GRUB, shim,
 * MokManager, the key); -1 if one is missing. */
int mount_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *s);
/* gapps (the Play Store), microg (microG's GmsCore without it) or none, from the running system's files:
 * the add-ons that bring or hide them are laid over it. */
const char *find_running_flavor(const struct sys_env *env);
/* 0 on an installed (not live) system. */
int find_running_install(const struct sys_env *env, const struct sys_ops *ops, struct running_install *out);
/* A live USB drive among parts (skipping the install's own partition), left mounted as the source. */
int find_usb_source(const struct sys_env *env, const struct sys_ops *ops, const struct partition *parts, int count,
		    const char *skip, struct live_source *out);
/* The build inside the source's system.img. */
int read_source_build(const struct sys_env *env, const struct sys_ops *ops, const struct live_source *s,
		      struct build_info *out);
