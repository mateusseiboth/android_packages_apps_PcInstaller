/*
 * Where pc-installd looks (sys_env) and the privileged calls it makes (sys_ops). The device uses
 * the real paths and linux_sys_ops(); the host tests point both at a temporary tree.
 */
#pragma once
#include <stddef.h>

struct sys_env {
	const char *sysfs;     /* /sys */
	const char *dev_dir;   /* /dev/block */
	const char *efivars;   /* /sys/firmware/efi/efivars */
	const char *work;      /* mount points: <work>/{probe,src,target,esp,image} */
	const char *mountinfo; /* /proc/self/mountinfo */
	const char *cmdline;   /* /proc/cmdline */
	const char *mke2fs;    /* /system/bin/mke2fs */
	const char *build_prop; /* /system/build.prop of the running system */
};

struct sys_ops {
	int (*mount)(const struct sys_env *env, const char *dev, const char *dir, const char *fs, int read_only);
	int (*umount)(const struct sys_env *env, const char *dir);
	/* Attaches file read-only to a free loop device; writes its /dev path to dev. */
	int (*attach_loop)(const struct sys_env *env, const char *file, char *dev, size_t cap);
	/* The device (major:minor) holding the file behind a loop device. */
	int (*find_loop_backing)(const struct sys_env *env, const char *loop_name, unsigned *major, unsigned *minor);
	int (*make_ext4)(const struct sys_env *env, const char *image);
	/* Android does not mount efivarfs; this mounts it on env->efivars unless it already is. */
	int (*mount_efivars)(const struct sys_env *env);
};

const struct sys_env *linux_sys_env(void);
const struct sys_ops *linux_sys_ops(void);
