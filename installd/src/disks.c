#include "disks.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "fsprobe.h"
#include "parttable.h"
#include "plan.h"
#include "util.h"

static int is_candidate(const char *name)
{
	static const char *const SKIPPED[] = { "loop", "ram", "zram", "dm-", "md", "sr", "mmcblk0boot", "mmcblk0rpmb" };
	for (size_t i = 0; i < sizeof SKIPPED / sizeof *SKIPPED; i++)
		if (!strncmp(name, SKIPPED[i], strlen(SKIPPED[i])))
			return 0;
	return 1;
}

static void build_sys_path(char *out, size_t cap, const struct sys_env *env, const char *name, const char *file)
{
	snprintf(out, cap, "%s/class/block/%s/%s", env->sysfs, name, file);
}

static int is_partition(const struct sys_env *env, const char *name)
{
	char path[PATH_MAX];
	build_sys_path(path, sizeof path, env, name, "partition");
	return is_file(path);
}

static void read_disk_name(const struct sys_env *env, const char *name, char *out, size_t cap)
{
	char path[PATH_MAX], real[PATH_MAX];
	build_sys_path(path, sizeof path, env, name, "..");
	snprintf(out, cap, "%s", realpath(path, real) ? strrchr(real, '/') + 1 : "");
}

static int read_head(const struct sys_env *env, const char *name, unsigned char *buf, size_t len)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", env->dev_dir, name);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? pread(fd, buf, len, 0) : -1;
	if (fd >= 0)
		close(fd);
	return (int)n;
}

static void probe_partition(const struct sys_env *env, struct partition *p)
{
	static unsigned char buf[PROBE_BYTES];
	struct fs_info fs;
	int n = read_head(env, p->name, buf, sizeof buf);
	probe_fs(buf, n > 0 ? (size_t)n : 0, &fs);
	snprintf(p->fs, sizeof p->fs, "%s", fs.type);
	snprintf(p->label, sizeof p->label, "%s", fs.label);
}

static void read_esp_flag(const struct sys_env *env, struct partition *p)
{
	static unsigned char head[PARTTABLE_HEAD_BYTES];
	char path[PATH_MAX];
	unsigned long long lbs = 512;
	snprintf(path, sizeof path, "%s/class/block/%s/queue/logical_block_size", env->sysfs, p->disk);
	read_u64(path, &lbs);
	struct part_table t;
	int n = read_head(env, p->disk, head, sizeof head);
	if (n <= 0 || parse_part_table(head, (size_t)n, (unsigned)lbs, &t))
		return;
	for (int i = 0; i < t.count; i++)
		p->is_esp |= t.entries[i].number == p->number && t.entries[i].is_esp;
}

/* Mounting read-only is the one way to learn the free space of every file system we accept. */
static void measure_free(const struct sys_env *env, const struct sys_ops *ops, struct partition *p)
{
	char dev[PATH_MAX], dir[PATH_MAX];
	struct statvfs st;
	if (find_fs_rule(p->fs)->reason)
		return;
	snprintf(dev, sizeof dev, "%s/%s", env->dev_dir, p->name);
	snprintf(dir, sizeof dir, "%s/probe", env->work);
	make_dirs(dir, 0700);
	if (ops->mount(env, dev, dir, find_mount_type(p->fs), 1))
		return;
	p->free = !statvfs(dir, &st) ? (unsigned long long)st.f_bavail * st.f_frsize : 0;
	ops->umount(env, dir);
}

static void read_partition(const struct sys_env *env, const struct sys_ops *ops, const char *name, struct partition *p)
{
	char path[PATH_MAX];
	unsigned long long v = 0;
	*p = (struct partition){ 0 };
	snprintf(p->name, sizeof p->name, "%.31s", name);
	read_disk_name(env, name, p->disk, sizeof p->disk);
	build_sys_path(path, sizeof path, env, name, "size");
	p->size = read_u64(path, &v) ? 0 : v * 512;
	build_sys_path(path, sizeof path, env, name, "partition");
	p->number = read_u64(path, &v) ? 0 : (int)v;
	probe_partition(env, p);
	read_esp_flag(env, p);
	measure_free(env, ops, p);
}

int find_partitions(const struct sys_env *env, const struct sys_ops *ops, struct partition *out, int max)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/class/block", env->sysfs);
	DIR *d = opendir(path);
	int count = 0;
	for (struct dirent *e; d && count < max && (e = readdir(d));) {
		if (e->d_name[0] == '.' || !is_candidate(e->d_name) || !is_partition(env, e->d_name))
			continue;
		read_partition(env, ops, e->d_name, &out[count++]);
	}
	if (d)
		closedir(d);
	return count;
}

const struct partition *find_partition(const struct partition *parts, int count, const char *name)
{
	for (int i = 0; i < count; i++)
		if (!strcmp(parts[i].name, name))
			return &parts[i];
	return NULL;
}

const struct partition *find_esp(const struct partition *parts, int count, const struct partition *target)
{
	const struct partition *first = NULL;
	for (int i = 0; i < count; i++) {
		if (!parts[i].is_esp)
			continue;
		if (!strcmp(parts[i].disk, target->disk))
			return &parts[i];
		first = first ? first : &parts[i];
	}
	return first;
}

void apply_source_mark(struct partition *parts, int count, const char *source)
{
	for (int i = 0; i < count; i++)
		parts[i].is_source |= !strcmp(parts[i].name, source) || !strcmp(parts[i].disk, source);
}
