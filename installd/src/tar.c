#include "tar.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define BLOCK 512
#define NAME_OFFSET 0
#define SIZE_OFFSET 124
#define SIZE_LEN 12
#define SUM_OFFSET 148
#define SUM_LEN 8
#define TYPE_OFFSET 156
#define MAGIC_OFFSET 257
#define PREFIX_OFFSET 345
#define PREFIX_LEN 155

/* Bytes read, short only at the end of the input; -1 on an error. */
static ssize_t read_full(int fd, void *buf, size_t len)
{
	size_t got = 0;
	while (got < len) {
		ssize_t n = read(fd, (char *)buf + got, len - got);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return -1;
		if (!n)
			break;
		got += (size_t)n;
	}
	return (ssize_t)got;
}

static int skip_bytes(int fd, unsigned long long len)
{
	char buf[64 * BLOCK];
	while (len) {
		size_t chunk = len < sizeof buf ? (size_t)len : sizeof buf;
		if (read_full(fd, buf, chunk) != (ssize_t)chunk)
			return -1;
		len -= chunk;
	}
	return 0;
}

void open_tar(struct tar_reader *t, int fd) { *t = (struct tar_reader){ .fd = fd }; }

/* ---- the header ----------------------------------------------------------------------- */

static int parse_octal(const unsigned char *p, size_t len, unsigned long long *out)
{
	unsigned long long n = 0;
	size_t i = 0;
	while (i < len && p[i] == ' ')
		i++;
	for (; i < len && p[i] >= '0' && p[i] <= '7'; i++)
		n = n << 3 | (unsigned long long)(p[i] - '0');
	for (; i < len; i++)
		if (p[i] != ' ' && p[i])
			return -1;
	*out = n;
	return 0;
}

/* GNU's base-256 form, for members of 8 GiB and more. */
static int parse_base256(const unsigned char *p, size_t len, unsigned long long *out)
{
	unsigned long long n = p[0] & 0x7f;
	for (size_t i = 1; i < len; i++) {
		if (n >> 56)
			return -1;
		n = n << 8 | p[i];
	}
	*out = n;
	return 0;
}

static int parse_size(const unsigned char *p, unsigned long long *out)
{
	return p[0] & 0x80 ? parse_base256(p, SIZE_LEN, out) : parse_octal(p, SIZE_LEN, out);
}

static int has_valid_sum(const unsigned char *h)
{
	unsigned long long want = 0, sum = 0;
	if (parse_octal(h + SUM_OFFSET, SUM_LEN, &want))
		return 0;
	for (size_t i = 0; i < BLOCK; i++)
		sum += i >= SUM_OFFSET && i < SUM_OFFSET + SUM_LEN ? ' ' : h[i];
	return sum == want;
}

static int is_zero_block(const unsigned char *h)
{
	for (size_t i = 0; i < BLOCK; i++)
		if (h[i])
			return 0;
	return 1;
}

/* Only plain files with short names: a package holds nothing else. */
static int is_plain_file(const unsigned char *h)
{
	return !memcmp(h + MAGIC_OFFSET, "ustar", 5) && (h[TYPE_OFFSET] == '0' || !h[TYPE_OFFSET]) &&
	       !h[PREFIX_OFFSET] && h[NAME_OFFSET];
}

static int parse_header(struct tar_reader *t, const unsigned char *h, struct tar_member *out)
{
	if (!has_valid_sum(h) || !is_plain_file(h) || parse_size(h + SIZE_OFFSET, &out->size))
		return -1;
	memcpy(out->name, h + NAME_OFFSET, TAR_NAME_MAX);
	out->name[TAR_NAME_MAX] = 0;
	t->left = out->size;
	t->padding = (BLOCK - out->size % BLOCK) % BLOCK;
	return 0;
}

int read_tar_member(struct tar_reader *t, struct tar_member *out)
{
	unsigned char h[BLOCK];
	if (skip_bytes(t->fd, t->left + t->padding))
		return -1;
	t->left = t->padding = 0;
	ssize_t n = read_full(t->fd, h, sizeof h);
	if (n == 0)
		return 1;
	if (n != BLOCK)
		return -1;
	if (is_zero_block(h))
		return 1;
	return parse_header(t, h, out);
}

ssize_t read_tar_data(struct tar_reader *t, void *buf, size_t cap)
{
	size_t want = t->left < cap ? (size_t)t->left : cap;
	if (!want)
		return 0;
	ssize_t n = read_full(t->fd, buf, want);
	if (n != (ssize_t)want)
		return -1;
	t->left -= (unsigned long long)n;
	return n;
}
