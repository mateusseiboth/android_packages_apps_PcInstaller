#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fsprobe.h"
#include "plan.h"
#include "util.h"

#define LOOP_MAJOR 7

/* mountinfo: ID PARENT MAJOR:MINOR ROOT MOUNTPOINT ...; the last mount on / wins. */
int parse_root_device(const char *mountinfo, unsigned *major, unsigned *minor)
{
	int found = -1;
	for (const char *line = mountinfo; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
		unsigned maj, min;
		char mountpoint[8];
		if (sscanf(line, "%*u %*u %u:%u %*s %7s", &maj, &min, mountpoint) != 3 || strcmp(mountpoint, "/"))
			continue;
		*major = maj;
		*minor = min;
		found = 0;
	}
	return found;
}

int parse_live_cmdline(const char *cmdline, char *folder, size_t cap)
{
	char buf[4096];
	int live = 0;
	snprintf(folder, cap, "lineage");
	snprintf(buf, sizeof buf, "%s", cmdline);
	for (char *save = NULL, *w = strtok_r(buf, " \t\n", &save); w; w = strtok_r(NULL, " \t\n", &save)) {
		live |= !strcmp(w, "pc.live=1");
		if (!strncmp(w, "pc.src=", 7))
			snprintf(folder, cap, "%s", w + 7);
	}
	return live;
}

static int read_device_name(const struct sys_env *env, unsigned major, unsigned minor, char *out, size_t cap)
{
	char path[PATH_MAX], real[PATH_MAX];
	snprintf(path, sizeof path, "%s/dev/block/%u:%u", env->sysfs, major, minor);
	if (!realpath(path, real))
		return -1;
	snprintf(out, cap, "%s", strrchr(real, '/') + 1);
	return 0;
}

static void read_parent_name(const struct sys_env *env, const char *name, char *out, size_t cap)
{
	char path[PATH_MAX], real[PATH_MAX];
	snprintf(path, sizeof path, "%s/class/block/%s/partition", env->sysfs, name);
	int is_part = is_file(path);
	snprintf(path, sizeof path, "%s/class/block/%s/..", env->sysfs, name);
	snprintf(out, cap, "%s", is_part && realpath(path, real) ? strrchr(real, '/') + 1 : name);
}

int find_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *out)
{
	static char text[64 * 1024];
	unsigned major = 0, minor = 0, bmajor = 0, bminor = 0;
	char loop[32];
	*out = (struct live_source){ 0 };
	if (read_text(env->cmdline, text, sizeof text) || !parse_live_cmdline(text, out->folder, sizeof out->folder))
		return -1;
	if (read_text(env->mountinfo, text, sizeof text) || parse_root_device(text, &major, &minor) || major != LOOP_MAJOR)
		return -1;
	snprintf(loop, sizeof loop, "loop%u", minor);
	if (ops->find_loop_backing(env, loop, &bmajor, &bminor) ||
	    read_device_name(env, bmajor, bminor, out->name, sizeof out->name))
		return -1;
	read_parent_name(env, out->name, out->disk, sizeof out->disk);
	return 0;
}

/* ---- mount and measure ---------------------------------------------------------------- */

static int mount_any(const struct sys_env *env, const struct sys_ops *ops, const char *dev, const char *dir)
{
	static const char *const FALLBACK[] = { "iso9660", "vfat", "ext4", "exfat", "ntfs3", "f2fs", "btrfs" };
	unsigned char buf[PROBE_BYTES];
	struct fs_info fs;
	FILE *f = fopen(dev, "rb");
	size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
	if (f)
		fclose(f);
	probe_fs(buf, n, &fs);
	if (!ops->mount(env, dev, dir, find_mount_type(fs.type), 1))
		return 0;
	for (size_t i = 0; i < sizeof FALLBACK / sizeof *FALLBACK; i++)
		if (!ops->mount(env, dev, dir, FALLBACK[i], 1))
			return 0;
	return -1;
}

static int measure_file(const char *dir, const char *name, unsigned long long *out)
{
	char path[PATH_MAX * 2];
	struct stat st;
	snprintf(path, sizeof path, "%s/%s", dir, name);
	if (stat(path, &st) || !S_ISREG(st.st_mode))
		return -1;
	*out = (unsigned long long)st.st_size;
	return 0;
}

int mount_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *s)
{
	char dev[PATH_MAX], mnt[PATH_MAX / 2];
	snprintf(dev, sizeof dev, "%s/%s", env->dev_dir, s->name);
	snprintf(mnt, sizeof mnt, "%s/src", env->work);
	make_dirs(mnt, 0700);
	if (mount_any(env, ops, dev, mnt))
		return -1;
	snprintf(s->dir, sizeof s->dir, "%s/%s", mnt, s->folder);
	struct source_sizes *z = &s->sizes;
	return measure_file(s->dir, "kernel", &z->kernel) || measure_file(s->dir, "initrd.img", &z->initrd) ||
			       measure_file(s->dir, "system.img", &z->system) || measure_file(s->dir, "grubx64.efi", &z->grub)
		       ? -1
		       : 0;
}
