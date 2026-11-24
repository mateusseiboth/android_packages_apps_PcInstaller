#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int read_text(const char *path, char *out, size_t cap)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, out, cap - 1) : -1;
	if (fd >= 0)
		close(fd);
	out[n > 0 ? n : 0] = 0;
	out[strcspn(out, "\n")] = 0;
	return n < 0 ? -1 : 0;
}

int read_u64(const char *path, unsigned long long *out)
{
	char text[32], *end = NULL;
	if (read_text(path, text, sizeof text) || !*text)
		return -1;
	*out = strtoull(text, &end, 10);
	return *end ? -1 : 0;
}

int is_dir(const char *path)
{
	struct stat st;
	return !stat(path, &st) && S_ISDIR(st.st_mode);
}

int is_file(const char *path)
{
	struct stat st;
	return !stat(path, &st) && S_ISREG(st.st_mode);
}

int make_dirs(const char *path, unsigned mode)
{
	char buf[PATH_MAX];
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(buf, mode) && errno != EEXIST)
			return -1;
		*p = '/';
	}
	return mkdir(buf, mode) && errno != EEXIST ? -1 : 0;
}
