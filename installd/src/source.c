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

/* UEFI: shim, MokManager and the builder's key go next to GRUB. */
static const char *const EFI_FILES[] = { "shimx64.efi", "mmx64.efi", "ENROLL_THIS_KEY_IN_MOKMANAGER.der" };

static int measure_efi_files(const char *dir)
{
	unsigned long long size = 0;
	for (size_t i = 0; i < sizeof EFI_FILES / sizeof *EFI_FILES; i++)
		if (measure_file(dir, EFI_FILES[i], &size))
			return -1;
	return 0;
}

/* Everything an install copies: the system, and GRUB for UEFI and for BIOS. */
static int measure_source(struct live_source *s)
{
	struct source_sizes *z = &s->sizes;
	char modules[PATH_MAX * 2];
	unsigned long long boot = 0;
	snprintf(modules, sizeof modules, "%s/grub-pc/i386-pc", s->dir);
	return measure_file(s->dir, "kernel", &z->kernel) || measure_file(s->dir, "initrd.img", &z->initrd) ||
			       measure_file(s->dir, "system.img", &z->system) ||
			       measure_file(s->dir, "grubx64.efi", &z->grub) || measure_efi_files(s->dir) ||
			       measure_file(s->dir, "grub-pc/core.img", &z->core) ||
			       measure_file(s->dir, "grub-pc/boot.img", &boot) || !is_dir(modules)
		       ? -1
		       : 0;
}

static void build_source_mount(char *out, size_t cap, const struct sys_env *env)
{
	snprintf(out, cap, "%s/src", env->work);
	make_dirs(out, 0700);
}

int mount_live_source(const struct sys_env *env, const struct sys_ops *ops, struct live_source *s)
{
	char dev[PATH_MAX], mnt[PATH_MAX / 2];
	snprintf(dev, sizeof dev, "%s/%s", env->dev_dir, s->name);
	build_source_mount(mnt, sizeof mnt, env);
	if (mount_any(env, ops, dev, mnt))
		return -1;
	snprintf(s->dir, sizeof s->dir, "%s/%s", mnt, s->folder);
	return measure_source(s);
}

/* ---- builds --------------------------------------------------------------------------- */

static void read_prop(const char *text, const char *key, char *out, size_t cap)
{
	size_t n = strlen(key);
	for (const char *line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
		if (strncmp(line, key, n) || line[n] != '=')
			continue;
		snprintf(out, cap, "%.*s", (int)strcspn(line + n + 1, "\n"), line + n + 1);
		return;
	}
}

void parse_build_prop(const char *text, struct build_info *out)
{
	char utc[32] = "";
	*out = (struct build_info){ 0 };
	read_prop(text, "ro.build.date.utc", utc, sizeof utc);
	out->utc = strtoull(utc, NULL, 10);
	read_prop(text, "ro.build.display.id", out->version, sizeof out->version);
	read_prop(text, "ro.lineage.display.version", out->version, sizeof out->version);
}

static int read_build_file(const char *path, struct build_info *out)
{
	static char text[64 * 1024];
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
	text[n] = 0;
	if (f)
		fclose(f);
	parse_build_prop(text, out);
	return f ? 0 : -1;
}

/* ---- an installed system -------------------------------------------------------------- */

static void parse_install_id(const char *cmdline, char *out, size_t cap)
{
	char buf[4096];
	out[0] = 0;
	snprintf(buf, sizeof buf, "%s", cmdline);
	for (char *save = NULL, *w = strtok_r(buf, " \t\n", &save); w; w = strtok_r(NULL, " \t\n", &save))
		if (!strncmp(w, "pc.id=", 6))
			snprintf(out, cap, "%s", w + 6);
}

/* / is system.img through a loop device, or the system/ folder bound from the partition itself. */
static int find_root_partition(const struct sys_env *env, const struct sys_ops *ops, const char *mountinfo,
			       char *out, size_t cap)
{
	unsigned major = 0, minor = 0;
	char loop[32];
	if (parse_root_device(mountinfo, &major, &minor))
		return -1;
	snprintf(loop, sizeof loop, "loop%u", minor);
	if (major == LOOP_MAJOR && ops->find_loop_backing(env, loop, &major, &minor))
		return -1;
	return read_device_name(env, major, minor, out, cap);
}

int find_running_install(const struct sys_env *env, const struct sys_ops *ops, struct running_install *out)
{
	static char text[64 * 1024];
	*out = (struct running_install){ 0 };
	if (read_text(env->cmdline, text, sizeof text) || parse_live_cmdline(text, out->folder, sizeof out->folder))
		return -1;
	parse_install_id(text, out->id, sizeof out->id);
	if (read_text(env->mountinfo, text, sizeof text) || find_root_partition(env, ops, text, out->part, sizeof out->part))
		return -1;
	read_build_file(env->build_prop, &out->build);
	return 0;
}

/* ---- a live USB drive plugged into an installed system -------------------------------- */

static int is_live_folder(const char *dir)
{
	char path[PATH_MAX + 32];
	snprintf(path, sizeof path, "%s/install-live.id", dir);
	return is_file(path);
}

static int try_usb_source(const struct sys_env *env, const struct sys_ops *ops, const struct partition *p,
			  struct live_source *out)
{
	*out = (struct live_source){ 0 };
	snprintf(out->name, sizeof out->name, "%s", p->name);
	snprintf(out->disk, sizeof out->disk, "%s", p->disk);
	snprintf(out->folder, sizeof out->folder, "%s", LIVE_FOLDER);
	if (mount_live_source(env, ops, out))
		return -1;
	if (is_live_folder(out->dir))
		return 0;
	char mnt[PATH_MAX / 2];
	build_source_mount(mnt, sizeof mnt, env);
	ops->umount(env, mnt);
	return -1;
}

int find_usb_source(const struct sys_env *env, const struct sys_ops *ops, const struct partition *parts, int count,
		    const char *skip, struct live_source *out)
{
	char mnt[PATH_MAX / 2];
	build_source_mount(mnt, sizeof mnt, env);
	for (int i = 0; i < count; i++) {
		if (!strcmp(parts[i].name, skip))
			continue;
		if (!try_usb_source(env, ops, &parts[i], out))
			return 0;
		ops->umount(env, mnt);
	}
	return -1;
}

int read_source_build(const struct sys_env *env, const struct sys_ops *ops, const struct live_source *s,
		      struct build_info *out)
{
	char image[PATH_MAX + 16], mnt[PATH_MAX / 2], dev[64], prop[PATH_MAX];
	snprintf(image, sizeof image, "%s/system.img", s->dir);
	snprintf(mnt, sizeof mnt, "%s/image", env->work);
	make_dirs(mnt, 0700);
	if (ops->attach_loop(env, image, dev, sizeof dev) || ops->mount(env, dev, mnt, "ext4", 1))
		return -1;
	snprintf(prop, sizeof prop, "%s/system/build.prop", mnt);
	int failed = read_build_file(prop, out);
	ops->umount(env, mnt);
	return failed;
}
