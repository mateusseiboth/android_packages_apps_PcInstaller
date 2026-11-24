/* A fake machine for the host tests: a sysfs tree, block device files, and mounts as symlinks. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sysops.h"
#include "treecopy.h"

static char fake_root[64];
static char fake_paths[8][160];
static struct sys_env fake_env;

static inline const char *fake_path(const char *rel)
{
	static char out[8][256];
	static int next;
	char *p = out[next++ % 8];
	snprintf(p, 256, "%s/%s", fake_root, rel);
	return p;
}

static inline void fake_write(const char *rel, const char *text)
{
	char cmd[512];
	snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname %s)\" && printf '%%s' '%s' > %s", fake_path(rel), text, fake_path(rel));
	if (system(cmd))
		fprintf(stderr, "fake_write %s failed\n", rel);
}

static inline void fake_sh(const char *fmt, const char *arg)
{
	char cmd[1024];
	snprintf(cmd, sizeof cmd, "cd %s && ", fake_root);
	snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), fmt, arg);
	if (system(cmd))
		fprintf(stderr, "fake_sh failed: %s\n", cmd);
}

/* A disk in sysfs with its partitions; the device files are created separately. */
static inline void fake_disk(const char *disk, int lbs)
{
	char rel[128], text[16];
	snprintf(rel, sizeof rel, "sys/devices/pci/block/%s/queue/logical_block_size", disk);
	snprintf(text, sizeof text, "%d", lbs);
	fake_write(rel, text);
	snprintf(rel, sizeof rel, "sys/devices/pci/block/%s/size", disk);
	fake_write(rel, "131072");
	fake_sh("mkdir -p sys/class/block && ln -s ../../devices/pci/block/%1$s sys/class/block/%1$s", disk);
}

static inline void fake_partition(const char *disk, const char *name, int number, unsigned long long sectors)
{
	char rel[128], text[32];
	snprintf(rel, sizeof rel, "sys/devices/pci/block/%s/%s/partition", disk, name);
	snprintf(text, sizeof text, "%d", number);
	fake_write(rel, text);
	snprintf(rel, sizeof rel, "sys/devices/pci/block/%s/%s/size", disk, name);
	snprintf(text, sizeof text, "%llu", sectors);
	fake_write(rel, text);
	char link[256];
	snprintf(link, sizeof link, "ln -s ../../devices/pci/block/%s/%%1$s sys/class/block/%%1$s", disk);
	fake_sh(link, name);
	fake_sh("mkdir -p mnt-%s", name);
}

/* "Mounting" links the mount point to mnt-<device name>. */
static inline int fake_mount(const struct sys_env *env, const char *dev, const char *dir, const char *fs, int read_only)
{
	(void)env;
	(void)fs;
	(void)read_only;
	char target[256];
	snprintf(target, sizeof target, "%s/mnt-%s", fake_root, strrchr(dev, '/') + 1);
	rmdir(dir);
	return symlink(target, dir);
}

static inline int fake_umount(const struct sys_env *env, const char *dir)
{
	(void)env;
	return unlink(dir) || mkdir(dir, 0755);
}

static inline int fake_attach_loop(const struct sys_env *env, const char *file, char *dev, size_t cap)
{
	(void)env;
	(void)file;
	snprintf(dev, cap, "/dev/block/loop9");
	return 0;
}

static unsigned fake_backing_major = 8, fake_backing_minor = 17;

static inline int fake_find_loop_backing(const struct sys_env *env, const char *loop, unsigned *major, unsigned *minor)
{
	(void)env;
	(void)loop;
	*major = fake_backing_major;
	*minor = fake_backing_minor;
	return 0;
}

static inline int fake_make_ext4(const struct sys_env *env, const char *image)
{
	(void)env;
	FILE *f = fopen(image, "r+");
	int ok = f && fseek(f, 1080, SEEK_SET) == 0 && fwrite("\x53\xef", 1, 2, f) == 2;
	if (f)
		fclose(f);
	return ok ? 0 : -1;
}

static int fake_efivars_mounts;

static inline int fake_mount_efivars(const struct sys_env *env)
{
	(void)env;
	fake_efivars_mounts++;
	return 0;
}

/* Always 42: the MOK password is 00000042. */
static inline int fake_fill_random(const struct sys_env *env, void *buf, size_t len)
{
	(void)env;
	memset(buf, 0, len);
	*(unsigned char *)buf = 42;
	return 0;
}

static const struct sys_ops FAKE_OPS = { fake_mount, fake_umount, fake_attach_loop, fake_find_loop_backing,
					 fake_make_ext4, fake_mount_efivars, fake_fill_random };

static inline void fake_setup(void)
{
	snprintf(fake_root, sizeof fake_root, "/tmp/fakesys-XXXXXX");
	if (!mkdtemp(fake_root))
		abort();
	static const char *const RELS[] = { "sys", "dev/block", "efivars", "work", "mountinfo", "cmdline", "mke2fs",
					    "build.prop" };
	for (int i = 0; i < 8; i++)
		snprintf(fake_paths[i], sizeof fake_paths[i], "%s/%s", fake_root, RELS[i]);
	fake_env = (struct sys_env){ fake_paths[0], fake_paths[1], fake_paths[2], fake_paths[3],
				     fake_paths[4], fake_paths[5], fake_paths[6], fake_paths[7] };
	fake_sh("mkdir -p sys/class/block dev/block efivars work && touch %s", "mountinfo");
}

static inline void fake_teardown(void) { remove_tree(fake_root); }

/* A device file whose first bytes carry a file system signature. */
static inline void fake_device(const char *name, const char *mkfs)
{
	char cmd[256];
	snprintf(cmd, sizeof cmd, "truncate -s 8M dev/block/%%1$s && %s dev/block/%%1$s >/dev/null 2>&1", mkfs);
	fake_sh(cmd, name);
}

/* A device file starting with the given bytes (a boot sector signature). */
static inline void fake_signature(const char *name, const char *bytes)
{
	char cmd[256];
	snprintf(cmd, sizeof cmd, "truncate -s 8M dev/block/%%1$s && printf '%s' | dd conv=notrunc of=dev/block/%%1$s 2>/dev/null",
		 bytes);
	fake_sh(cmd, name);
}
