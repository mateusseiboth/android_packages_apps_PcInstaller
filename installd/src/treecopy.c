#define _GNU_SOURCE
#include "treecopy.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/xattr.h>
#include <unistd.h>

#define CHUNK (1 << 20)

/* ---- inodes already seen (hardlinks) --------------------------------------------------- */

struct seen {
	dev_t dev;
	ino_t ino;
	char *path;
};

struct inode_set {
	struct seen *items;
	size_t count, cap;
};

static const struct seen *find_seen(const struct inode_set *s, const struct stat *st)
{
	for (size_t i = 0; i < s->count; i++)
		if (s->items[i].dev == st->st_dev && s->items[i].ino == st->st_ino)
			return &s->items[i];
	return NULL;
}

static int add_seen(struct inode_set *s, const struct stat *st, const char *path)
{
	if (s->count == s->cap) {
		size_t cap = s->cap ? s->cap * 2 : 64;
		struct seen *items = realloc(s->items, cap * sizeof *items);
		if (!items)
			return -1;
		s->items = items;
		s->cap = cap;
	}
	s->items[s->count++] = (struct seen){ st->st_dev, st->st_ino, strdup(path) };
	return 0;
}

static void free_seen(struct inode_set *s)
{
	for (size_t i = 0; i < s->count; i++)
		free(s->items[i].path);
	free(s->items);
}

/* ---- one file ------------------------------------------------------------------------- */

static int is_zero(const char *buf, size_t n)
{
	return !buf[0] && !memcmp(buf, buf + 1, n - 1);
}

static void report(copy_progress progress, void *ctx, unsigned long long bytes)
{
	if (progress)
		progress(ctx, bytes);
}

/* Zero chunks become holes, so a mostly empty image takes little room on the target. */
static int copy_content(int in, int out, copy_progress progress, void *ctx)
{
	char *buf = malloc(CHUNK);
	off_t size = 0;
	ssize_t n = buf ? 1 : -1;
	while (n > 0 && (n = read(in, buf, CHUNK)) > 0) {
		int skip = is_zero(buf, (size_t)n);
		ssize_t done = skip ? (lseek(out, n, SEEK_CUR) < 0 ? -1 : n) : write(out, buf, (size_t)n);
		n = done == n ? n : -1;
		size += n > 0 ? n : 0;
		report(progress, ctx, n > 0 ? (unsigned long long)n : 0);
	}
	free(buf);
	return n < 0 || ftruncate(out, size) || fsync(out) ? -1 : 0;
}

static int copy_fd_to_path(int in, const char *dst, mode_t mode, copy_progress progress, void *ctx)
{
	int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode & 07777);
	if (out < 0)
		return -1;
	int failed = copy_content(in, out, progress, ctx);
	return close(out) || failed ? -1 : 0;
}

int copy_file(const char *src, const char *dst, copy_progress progress, void *ctx)
{
	int in = open(src, O_RDONLY | O_CLOEXEC);
	if (in < 0)
		return -1;
	int failed = copy_fd_to_path(in, dst, 0644, progress, ctx);
	close(in);
	return failed;
}

/* ---- metadata ------------------------------------------------------------------------- */

static int apply_owner(const char *dst, const struct stat *st) { return lchown(dst, st->st_uid, st->st_gid); }

static int apply_mode(const char *dst, const struct stat *st)
{
	return S_ISLNK(st->st_mode) ? 0 : chmod(dst, st->st_mode & 07777);
}

/* After the owner: chown drops security.capability. */
static int apply_xattrs(const char *src, const char *dst)
{
	char names[8192], value[8192];
	ssize_t len = llistxattr(src, names, sizeof names);
	if (len < 0)
		return errno == ENOTSUP ? 0 : -1;
	for (char *name = names; name < names + len; name += strlen(name) + 1) {
		ssize_t n = lgetxattr(src, name, value, sizeof value);
		if (n < 0 || lsetxattr(dst, name, value, (size_t)n, 0))
			return -1;
	}
	return 0;
}

static int apply_times(const char *dst, const struct stat *st)
{
	struct timespec times[2] = { st->st_atim, st->st_mtim };
	return utimensat(AT_FDCWD, dst, times, AT_SYMLINK_NOFOLLOW);
}

static int apply_metadata(const char *src, const char *dst, const struct stat *st)
{
	return apply_owner(dst, st) || apply_mode(dst, st) || apply_xattrs(src, dst) || apply_times(dst, st) ? -1 : 0;
}

/* ---- tree ----------------------------------------------------------------------------- */

struct tree_copy {
	copy_progress progress;
	void *ctx;
	struct inode_set links;
};

static int copy_entry(struct tree_copy *t, const char *src, const char *dst);

static int copy_children(struct tree_copy *t, const char *src, const char *dst)
{
	DIR *d = opendir(src);
	if (!d)
		return -1;
	int failed = 0;
	for (struct dirent *e; !failed && (e = readdir(d));) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		char from[PATH_MAX], to[PATH_MAX];
		snprintf(from, sizeof from, "%s/%s", src, e->d_name);
		snprintf(to, sizeof to, "%s/%s", dst, e->d_name);
		failed = copy_entry(t, from, to);
	}
	closedir(d);
	return failed;
}

/* The directory's times go last: creating its children changes them. */
static int copy_dir(struct tree_copy *t, const char *src, const char *dst, const struct stat *st)
{
	if (mkdir(dst, 0700) && errno != EEXIST)
		return -1;
	if (copy_children(t, src, dst))
		return -1;
	return apply_metadata(src, dst, st);
}

static int copy_regular(struct tree_copy *t, const char *src, const char *dst, const struct stat *st)
{
	int in = open(src, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (in < 0)
		return -1;
	int failed = copy_fd_to_path(in, dst, 0600, t->progress, t->ctx);
	close(in);
	return failed || apply_metadata(src, dst, st);
}

static int copy_symlink(struct tree_copy *t, const char *src, const char *dst, const struct stat *st)
{
	(void)t;
	char target[PATH_MAX];
	ssize_t n = readlink(src, target, sizeof target - 1);
	if (n < 0)
		return -1;
	target[n] = 0;
	return symlink(target, dst) || apply_metadata(src, dst, st) ? -1 : 0;
}

static int copy_special(struct tree_copy *t, const char *src, const char *dst, const struct stat *st)
{
	(void)t;
	return mknod(dst, st->st_mode, st->st_rdev) || apply_metadata(src, dst, st) ? -1 : 0;
}

typedef int (*entry_copier)(struct tree_copy *, const char *, const char *, const struct stat *);

static entry_copier find_copier(mode_t mode)
{
	static const struct {
		mode_t type;
		entry_copier copy;
	} COPIERS[] = { { S_IFDIR, copy_dir }, { S_IFREG, copy_regular }, { S_IFLNK, copy_symlink } };
	for (size_t i = 0; i < sizeof COPIERS / sizeof *COPIERS; i++)
		if ((mode & S_IFMT) == COPIERS[i].type)
			return COPIERS[i].copy;
	return copy_special;
}

static int copy_entry(struct tree_copy *t, const char *src, const char *dst)
{
	struct stat st;
	if (lstat(src, &st))
		return -1;
	const struct seen *same = !S_ISDIR(st.st_mode) && st.st_nlink > 1 ? find_seen(&t->links, &st) : NULL;
	if (same)
		return link(same->path, dst);
	if (!S_ISDIR(st.st_mode) && st.st_nlink > 1 && add_seen(&t->links, &st, dst))
		return -1;
	return find_copier(st.st_mode)(t, src, dst, &st);
}

int copy_tree(const char *src, const char *dst, copy_progress progress, void *ctx)
{
	struct tree_copy t = { progress, ctx, { 0 } };
	int failed = copy_entry(&t, src, dst);
	free_seen(&t.links);
	return failed;
}

int copy_flat_dir(const char *src, const char *dst)
{
	DIR *d = opendir(src);
	int failed = !d;
	for (struct dirent *e; !failed && (e = readdir(d));) {
		char from[PATH_MAX], to[PATH_MAX];
		struct stat st;
		snprintf(from, sizeof from, "%s/%s", src, e->d_name);
		snprintf(to, sizeof to, "%s/%s", dst, e->d_name);
		failed = !stat(from, &st) && S_ISREG(st.st_mode) && copy_file(from, to, NULL, NULL);
	}
	if (d)
		closedir(d);
	return failed ? -1 : 0;
}

/* ---- measure and remove --------------------------------------------------------------- */

static unsigned long long measure_entry(const char *path, struct inode_set *seen)
{
	struct stat st;
	if (lstat(path, &st))
		return 0;
	if (S_ISREG(st.st_mode))
		return st.st_nlink > 1 && (find_seen(seen, &st) || add_seen(seen, &st, path)) ? 0 : (unsigned long long)st.st_size;
	if (!S_ISDIR(st.st_mode))
		return 0;
	unsigned long long total = 0;
	DIR *d = opendir(path);
	for (struct dirent *e; d && (e = readdir(d));) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		char child[PATH_MAX];
		snprintf(child, sizeof child, "%s/%s", path, e->d_name);
		total += measure_entry(child, seen);
	}
	if (d)
		closedir(d);
	return total;
}

unsigned long long measure_tree(const char *dir)
{
	struct inode_set seen = { 0 };
	unsigned long long total = measure_entry(dir, &seen);
	free_seen(&seen);
	return total;
}

static int remove_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	return remove(path);
}

int remove_tree(const char *path)
{
	struct stat st;
	if (lstat(path, &st))
		return errno == ENOENT ? 0 : -1;
	return nftw(path, remove_one, 32, FTW_DEPTH | FTW_PHYS);
}
