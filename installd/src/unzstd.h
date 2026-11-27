/*
 * A zstd stream decompressed into a file as it arrives, its output hashed and its zero blocks left as
 * holes. libzstd is loaded at run time: Android ships it (/system/lib64/libzstd.so), but
 * external/zstd's Android.bp keeps the library out of reach of this package (visibility).
 */
#pragma once
#include <stddef.h>

#include "pkgsig.h"

struct unzstd {
	void *dctx;
	int fd;
	unsigned long long written;
	size_t last;  /* ZSTD_decompressStream's last result: 0 once a frame is complete */
	int is_full;  /* the last call filled the output buffer: the decoder may hold more */
	unsigned char *out;
	struct sha256_stream hash;
};

/* write_unzstd and finish_unzstd: 0, -1 when the image cannot be written (errno set), or this when the
 * input is not a whole zstd stream. */
#define UNZSTD_BAD_STREAM (-2)

/* fd: the image being written, empty. -1 when libzstd cannot be loaded. */
int open_unzstd(struct unzstd *z, int fd);
int write_unzstd(struct unzstd *z, const void *data, size_t len);
/* Checks that the last frame is complete; also frees z (not the fd). */
int finish_unzstd(struct unzstd *z, char sha256[SHA256_HEX], unsigned long long *size);
void close_unzstd(struct unzstd *z);
