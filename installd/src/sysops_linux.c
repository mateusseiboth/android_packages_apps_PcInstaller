#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/loop.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include "efiboot.h"
#include "sysops.h"

static int linux_mount(const struct sys_env *env, const char *dev, const char *dir, const char *fs, int read_only)
{
	(void)env;
	unsigned long flags = MS_NOATIME | MS_NODEV | MS_NOSUID | (read_only ? MS_RDONLY : 0);
	return mount(dev, dir, fs, flags, NULL);
}

static int linux_umount(const struct sys_env *env, const char *dir)
{
	(void)env;
	return umount2(dir, 0) && umount2(dir, MNT_DETACH) ? -1 : 0;
}

/* Read-only and cleared on the last close or unmount, like pc-init's. */
static int linux_attach_loop(const struct sys_env *env, const char *file, char *dev, size_t cap)
{
	int ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
	int n = ctl >= 0 ? ioctl(ctl, LOOP_CTL_GET_FREE) : -1;
	if (ctl >= 0)
		close(ctl);
	if (n < 0)
		return -1;
	snprintf(dev, cap, "%s/loop%d", env->dev_dir, n);
	int backing = open(file, O_RDONLY | O_CLOEXEC);
	int loop = open(dev, O_RDONLY | O_CLOEXEC);
	struct loop_config cfg = { .fd = (unsigned)backing,
				   .info = { .lo_flags = LO_FLAGS_AUTOCLEAR | LO_FLAGS_READ_ONLY } };
	int failed = backing < 0 || loop < 0 || ioctl(loop, LOOP_CONFIGURE, &cfg);
	if (backing >= 0)
		close(backing);
	if (loop >= 0)
		close(loop);
	return failed ? -1 : 0;
}

static int linux_find_loop_backing(const struct sys_env *env, const char *loop_name, unsigned *major_out,
				   unsigned *minor_out)
{
	char dev[256];
	struct loop_info64 info;
	snprintf(dev, sizeof dev, "%s/%s", env->dev_dir, loop_name);
	int fd = open(dev, O_RDONLY | O_CLOEXEC);
	int failed = fd < 0 || ioctl(fd, LOOP_GET_STATUS64, &info);
	if (fd >= 0)
		close(fd);
	if (failed)
		return -1;
	*major_out = major((dev_t)info.lo_device);
	*minor_out = minor((dev_t)info.lo_device);
	return 0;
}

static int linux_make_ext4(const struct sys_env *env, const char *image)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp(env->mke2fs, env->mke2fs, "-q", "-F", "-t", "ext4", "-b", "4096", "-L", "data", image, (char *)NULL);
		_exit(127);
	}
	int status = 0;
	return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}

#define EFIVARFS_MAGIC 0xde5e81e4

static int linux_mount_efivars(const struct sys_env *env)
{
	struct statfs st;
	if (!statfs(env->efivars, &st) && (unsigned long)st.f_type == EFIVARFS_MAGIC)
		return 0;
	return mount("efivarfs", env->efivars, "efivarfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
}

static int linux_fill_random(const struct sys_env *env, void *buf, size_t len)
{
	(void)env;
	return getrandom(buf, len, 0) == (ssize_t)len ? 0 : -1;
}

static const struct sys_ops LINUX_OPS = { linux_mount, linux_umount, linux_attach_loop, linux_find_loop_backing,
					  linux_make_ext4, linux_mount_efivars, linux_fill_random };

static const struct sys_env LINUX_ENV = { "/sys", "/dev/block", EFIVARS_DIR, "/mnt/pc-installd",
					  "/proc/self/mountinfo", "/proc/cmdline", "/system/bin/mke2fs",
					  "/system/build.prop" };

const struct sys_env *linux_sys_env(void) { return &LINUX_ENV; }
const struct sys_ops *linux_sys_ops(void) { return &LINUX_OPS; }
