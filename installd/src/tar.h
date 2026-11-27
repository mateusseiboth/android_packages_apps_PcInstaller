/*
 * A POSIX ustar archive read strictly front to back with read(): the app may hand over a pipe, so the
 * reader never seeks and never reads anything twice.
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>

#define TAR_NAME_MAX 100

struct tar_reader {
	int fd;
	unsigned long long left;    /* bytes of the current member not read yet */
	unsigned long long padding; /* after them, up to the next 512-byte block */
};

struct tar_member {
	char name[TAR_NAME_MAX + 1];
	unsigned long long size;
};

void open_tar(struct tar_reader *t, int fd);
/* The next member's header, after skipping what is left of the current one: 0, 1 at the end of the
 * archive, -1 when the archive is cut short, malformed, or the member is not a regular file. */
int read_tar_member(struct tar_reader *t, struct tar_member *out);
/* Up to cap bytes of the current member: 0 at its end, -1 on a read error or a cut-off archive. */
ssize_t read_tar_data(struct tar_reader *t, void *buf, size_t cap);
