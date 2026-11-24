#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "check.h"
#include "treecopy.h"

static char base[64];
static char src[128], dst[128];

static void at(char *out, const char *root, const char *rel) { snprintf(out, 256, "%s/%s", root, rel); }

static void write_file(const char *rel, const char *text, mode_t mode)
{
	char p[256];
	at(p, src, rel);
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text));
	close(fd);
	chmod(p, mode);
}

static void setup(void)
{
	snprintf(base, sizeof base, "/tmp/treecopy-XXXXXX");
	CHECK(mkdtemp(base) != NULL);
	snprintf(src, sizeof src, "%s/src", base);
	snprintf(dst, sizeof dst, "%s/dst", base);
	mkdir(src, 0755);
	char p[256];
	at(p, src, "bin");
	mkdir(p, 0751);
	at(p, src, "etc");
	mkdir(p, 0755);
	at(p, src, "empty");
	mkdir(p, 0700);
	write_file("bin/tool", "#!/bin/sh\necho hi\n", 04755);
	write_file("etc/hosts", "127.0.0.1 localhost\n", 0644);
	char l[256];
	at(p, src, "bin/tool");
	at(l, src, "bin/tool-link");
	CHECK(!link(p, l));
	at(l, src, "etc/tool");
	CHECK(!symlink("../bin/tool", l));
	at(p, src, "etc/hosts");
	struct timespec times[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
	utimensat(AT_FDCWD, p, times, 0);
}

static void teardown(void)
{
	CHECK(!remove_tree(base));
	struct stat st;
	CHECK(lstat(base, &st));
}

static void stat_rel(const char *root, const char *rel, struct stat *st)
{
	char p[256];
	at(p, root, rel);
	CHECK(!lstat(p, st));
}

static void counts_progress(void *ctx, unsigned long long bytes) { *(unsigned long long *)ctx += bytes; }

static void copies_files_modes_and_times(void)
{
	setup();
	unsigned long long copied = 0;
	CHECK(!copy_tree(src, dst, counts_progress, &copied));
	CHECK(copied == measure_tree(src));
	struct stat a, b;
	stat_rel(dst, "bin/tool", &b);
	CHECK((b.st_mode & 07777) == 04755);
	stat_rel(dst, "bin", &b);
	CHECK((b.st_mode & 07777) == 0751 && S_ISDIR(b.st_mode));
	stat_rel(dst, "empty", &b);
	CHECK((b.st_mode & 07777) == 0700);
	stat_rel(src, "etc/hosts", &a);
	stat_rel(dst, "etc/hosts", &b);
	CHECK(b.st_size == a.st_size && b.st_mtim.tv_sec == 1000000000);
	stat_rel(src, "bin", &a);
	stat_rel(dst, "bin", &b);
	CHECK(b.st_mtim.tv_sec == a.st_mtim.tv_sec);
	teardown();
}

static void keeps_symlinks_and_hardlinks(void)
{
	setup();
	CHECK(!copy_tree(src, dst, NULL, NULL));
	char p[256], target[64] = { 0 };
	at(p, dst, "etc/tool");
	CHECK(readlink(p, target, sizeof target - 1) > 0);
	CHECK_STR(target, "../bin/tool");
	struct stat a, b;
	stat_rel(dst, "bin/tool", &a);
	stat_rel(dst, "bin/tool-link", &b);
	CHECK(a.st_ino == b.st_ino && a.st_nlink == 2);
	teardown();
}

static void keeps_extended_attributes(void)
{
	setup();
	char p[256];
	at(p, src, "etc/hosts");
	if (setxattr(p, "user.pc.test", "valor", 5, 0)) {
		printf("skip keeps_extended_attributes (no user xattrs on /tmp)\n");
		teardown();
		return;
	}
	CHECK(!copy_tree(src, dst, NULL, NULL));
	char value[16] = { 0 };
	at(p, dst, "etc/hosts");
	CHECK(getxattr(p, "user.pc.test", value, sizeof value) == 5);
	CHECK_STR(value, "valor");
	teardown();
}

static void copies_one_file_and_keeps_holes(void)
{
	setup();
	char from[256], to[256];
	at(from, src, "sparse.img");
	at(to, src, "copy.img");
	int fd = open(from, O_WRONLY | O_CREAT, 0644);
	CHECK(fd >= 0 && !ftruncate(fd, 64 << 20) && pwrite(fd, "end", 3, (64 << 20) - 3) == 3);
	close(fd);
	unsigned long long copied = 0;
	CHECK(!copy_file(from, to, counts_progress, &copied));
	struct stat st;
	CHECK(!stat(to, &st) && st.st_size == 64 << 20);
	CHECK(copied == 64 << 20);
	CHECK(st.st_blocks * 512 < (8 << 20));
	char tail[4] = { 0 };
	fd = open(to, O_RDONLY);
	CHECK(pread(fd, tail, 3, (64 << 20) - 3) == 3);
	close(fd);
	CHECK_STR(tail, "end");
	teardown();
}

static void a_missing_source_fails(void)
{
	CHECK(copy_tree("/nonexistent/pc-installd", "/tmp/never", NULL, NULL));
	CHECK(copy_file("/nonexistent/pc-installd", "/tmp/never", NULL, NULL));
}

int main(void)
{
	RUN(copies_files_modes_and_times);
	RUN(keeps_symlinks_and_hardlinks);
	RUN(keeps_extended_attributes);
	RUN(copies_one_file_and_keeps_holes);
	RUN(a_missing_source_fails);
	return CHECK_DONE();
}
