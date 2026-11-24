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
	snprintf(p->uuid, sizeof p->uuid, "%s", fs.uuid);
}

unsigned read_block_size(const struct sys_env *env, const char *disk)
{
	char path[PATH_MAX];
	unsigned long long lbs = 512;
	snprintf(path, sizeof path, "%s/class/block/%s/queue/logical_block_size", env->sysfs, disk);
	read_u64(path, &lbs);
	return (unsigned)lbs;
}

int read_part_table(const struct sys_env *env, const char *disk, struct part_table *t)
{
	static unsigned char head[PARTTABLE_HEAD_BYTES];
	char path[PATH_MAX];
	unsigned long long lbs = 512;
	snprintf(path, sizeof path, "%s/class/block/%s/queue/logical_block_size", env->sysfs, disk);
	read_u64(path, &lbs);
	int n = read_head(env, disk, head, sizeof head);
	return n <= 0 ? -1 : parse_part_table(head, (size_t)n, (unsigned)lbs, t);
}

static void read_esp_flag(const struct sys_env *env, struct partition *p)
{
	struct part_table t;
	if (read_part_table(env, p->disk, &t))
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
	if (ops->mount(env, dev, dir, find_mount_type(p->fs), 1)) {
		p->cannot_mount = 1;
		return;
	}
	p->cannot_mount = statvfs(dir, &st) != 0;
	p->free = p->cannot_mount ? 0 : (unsigned long long)st.f_bavail * st.f_frsize;
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

/* A disk formatted as a whole, without a partition table (a USB stick, say). */
static int read_whole_disk(const struct sys_env *env, const struct sys_ops *ops, const char *name, struct partition *p)
{
	*p = (struct partition){ 0 };
	snprintf(p->name, sizeof p->name, "%.31s", name);
	snprintf(p->disk, sizeof p->disk, "%.31s", name);
	probe_partition(env, p);
	if (!strcmp(p->fs, "unknown"))
		return -1;
	char path[PATH_MAX];
	unsigned long long v = 0;
	build_sys_path(path, sizeof path, env, name, "size");
	p->size = read_u64(path, &v) ? 0 : v * 512;
	measure_free(env, ops, p);
	return 0;
}

static int has_listed_partition(const struct partition *parts, int count, const char *disk)
{
	for (int i = 0; i < count; i++)
		if (!strcmp(parts[i].disk, disk))
			return 1;
	return 0;
}

typedef int (*entry_visitor)(const struct sys_env *env, const struct sys_ops *ops, const char *name,
			     struct partition *out, int count, int max);

static int visit_partition(const struct sys_env *env, const struct sys_ops *ops, const char *name,
			   struct partition *out, int count, int max)
{
	if (count >= max || !is_partition(env, name))
		return count;
	read_partition(env, ops, name, &out[count]);
	return count + 1;
}

static int visit_whole_disk(const struct sys_env *env, const struct sys_ops *ops, const char *name,
			    struct partition *out, int count, int max)
{
	if (count >= max || is_partition(env, name) || has_listed_partition(out, count, name))
		return count;
	return read_whole_disk(env, ops, name, &out[count]) ? count : count + 1;
}

static int visit_block_devices(const struct sys_env *env, const struct sys_ops *ops, entry_visitor visit,
			       struct partition *out, int count, int max)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/class/block", env->sysfs);
	DIR *d = opendir(path);
	for (struct dirent *e; d && (e = readdir(d));)
		if (e->d_name[0] != '.' && is_candidate(e->d_name))
			count = visit(env, ops, e->d_name, out, count, max);
	if (d)
		closedir(d);
	return count;
}

/* Partitions first, then disks that have none of their own. */
int find_partitions(const struct sys_env *env, const struct sys_ops *ops, struct partition *out, int max)
{
	int count = visit_block_devices(env, ops, visit_partition, out, 0, max);
	return visit_block_devices(env, ops, visit_whole_disk, out, count, max);
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
