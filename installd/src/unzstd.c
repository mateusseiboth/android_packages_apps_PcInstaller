#define _GNU_SOURCE
#include "unzstd.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

/* zstd.h's stable streaming API, declared here because its header is out of reach too. */
struct zstd_in {
	const void *src;
	size_t size, pos;
};

struct zstd_out {
	void *dst;
	size_t size, pos;
};

/* ZSTD_d_windowLogMax; make_update.py compresses with --long=27, the default limit. */
#define ZSTD_WINDOW_LOG_MAX 100
#define WINDOW_LOG 27
#define OUT_SIZE (256 * 1024)
#define HOLE_BLOCK 4096

static struct {
	int loaded;
	void *(*create)(void);
	size_t (*release)(void *);
	size_t (*decompress)(void *, struct zstd_out *, struct zstd_in *);
	unsigned (*is_error)(size_t);
	size_t (*set_parameter)(void *, int, int);
} api;

static const struct {
	const char *symbol;
	size_t offset;
} SYMBOLS[] = {
	{ "ZSTD_createDCtx", offsetof(__typeof__(api), create) },
	{ "ZSTD_freeDCtx", offsetof(__typeof__(api), release) },
	{ "ZSTD_decompressStream", offsetof(__typeof__(api), decompress) },
	{ "ZSTD_isError", offsetof(__typeof__(api), is_error) },
	{ "ZSTD_DCtx_setParameter", offsetof(__typeof__(api), set_parameter) },
};

static void *open_library(void)
{
	static const char *const NAMES[] = { "libzstd.so", "libzstd.so.1" };
	void *lib = NULL;
	for (size_t i = 0; !lib && i < sizeof NAMES / sizeof *NAMES; i++)
		lib = dlopen(NAMES[i], RTLD_NOW | RTLD_LOCAL);
	return lib;
}

static int load_api(void)
{
	void *lib = api.loaded ? NULL : open_library();
	for (size_t i = 0; lib && i < sizeof SYMBOLS / sizeof *SYMBOLS; i++) {
		void *fn = dlsym(lib, SYMBOLS[i].symbol);
		if (!fn)
			return -1;
		memcpy((char *)&api + SYMBOLS[i].offset, &fn, sizeof fn);
	}
	api.loaded |= lib != NULL;
	return api.loaded ? 0 : -1;
}

/* ---- writing with holes --------------------------------------------------------------- */

static int is_zero(const unsigned char *p, size_t len) { return !*p && !memcmp(p, p + 1, len - 1); }

/* Pieces end on HOLE_BLOCK boundaries of the file; a zero piece is skipped and stays a hole. */
static int write_out(struct unzstd *z, const unsigned char *p, size_t len)
{
	while (len) {
		size_t piece = HOLE_BLOCK - (size_t)(z->written % HOLE_BLOCK);
		piece = piece < len ? piece : len;
		int failed = is_zero(p, piece) ? lseek(z->fd, (off_t)piece, SEEK_CUR) < 0 : write_all(z->fd, p, piece);
		if (failed)
			return -1;
		z->written += piece;
		p += piece;
		len -= piece;
	}
	return 0;
}

/* ---- the stream ----------------------------------------------------------------------- */

int open_unzstd(struct unzstd *z, int fd)
{
	*z = (struct unzstd){ .fd = fd, .last = 1 };
	if (load_api())
		return -1;
	z->dctx = api.create();
	z->out = malloc(OUT_SIZE);
	if (!z->dctx || !z->out || api.is_error(api.set_parameter(z->dctx, ZSTD_WINDOW_LOG_MAX, WINDOW_LOG)) ||
	    open_sha256_stream(&z->hash)) {
		close_unzstd(z);
		return -1;
	}
	return 0;
}

static int drain(struct unzstd *z, struct zstd_in *in)
{
	struct zstd_out out = { z->out, OUT_SIZE, 0 };
	z->last = api.decompress(z->dctx, &out, in);
	z->is_full = out.pos == OUT_SIZE;
	if (api.is_error(z->last))
		return UNZSTD_BAD_STREAM;
	return update_sha256_stream(&z->hash, z->out, out.pos) || write_out(z, z->out, out.pos) ? -1 : 0;
}

int write_unzstd(struct unzstd *z, const void *data, size_t len)
{
	struct zstd_in in = { data, len, 0 };
	int failed = 0;
	while (!failed && in.pos < in.size)
		failed = drain(z, &in);
	return failed;
}

/* A full output buffer may leave more in the decoder; after that, the last frame must be complete. */
static int flush(struct unzstd *z)
{
	struct zstd_in in = { NULL, 0, 0 };
	int failed = 0;
	while (!failed && z->is_full)
		failed = drain(z, &in);
	return failed ? failed : z->last ? UNZSTD_BAD_STREAM : 0;
}

int finish_unzstd(struct unzstd *z, char sha256[SHA256_HEX], unsigned long long *size)
{
	int failed = flush(z);
	if (!failed && (ftruncate(z->fd, (off_t)z->written) || finish_sha256_stream(&z->hash, sha256)))
		failed = -1;
	*size = z->written;
	close_unzstd(z);
	return failed;
}

void close_unzstd(struct unzstd *z)
{
	if (z->dctx)
		api.release(z->dctx);
	close_sha256_stream(&z->hash);
	free(z->out);
	z->dctx = NULL;
	z->out = NULL;
}
