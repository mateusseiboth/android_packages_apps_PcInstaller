#include "package.h"

#include <stdlib.h>
#include <string.h>

#include "pkgsig.h"

#define SIG_MAX_BYTES 4096
#define CHUNK (1024 * 1024)

/* A small member read whole; -1 when it is not the expected one or is too large. */
static int load_member(struct package *p, const char *name, size_t max, unsigned char **data, size_t *len)
{
	struct tar_member m;
	if (read_tar_member(&p->tar, &m) || strcmp(m.name, name) || !m.size || m.size > max)
		return -1;
	*data = malloc((size_t)m.size + 1);
	if (!*data || read_tar_data(&p->tar, *data, (size_t)m.size) != (ssize_t)m.size) {
		free(*data);
		*data = NULL;
		return -1;
	}
	(*data)[m.size] = 0;
	*len = (size_t)m.size;
	return 0;
}

static const char *const SIGNATURE_ERRORS[] = {
	[SIGNATURE_GOOD] = NULL,
	[SIGNATURE_BAD] = "bad_signature",
	[SIGNATURE_NO_KEY] = "no_certificate",
};

static const char *check_signature(struct package *p, const unsigned char *cert, size_t cert_len)
{
	unsigned char *sig = NULL;
	size_t sig_len = 0;
	if (load_member(p, "manifest.sig", SIG_MAX_BYTES, &sig, &sig_len))
		return "bad_package";
	enum signature_result r = verify_signature(cert, cert_len, p->json, p->json_len, sig, sig_len);
	free(sig);
	return SIGNATURE_ERRORS[r];
}

const char *open_package(struct package *p, int fd, const unsigned char *cert, size_t cert_len)
{
	*p = (struct package){ 0 };
	open_tar(&p->tar, fd);
	if (load_member(p, "manifest.json", MANIFEST_MAX_BYTES, (unsigned char **)&p->json, &p->json_len) ||
	    strlen(p->json) != p->json_len)
		return "bad_package";
	const char *error = check_signature(p, cert, cert_len);
	if (error)
		return error;
	return parse_manifest(p->json, p->json_len, &p->manifest) ? "bad_package" : NULL;
}

/* ---- the listed files ----------------------------------------------------------------- */

static const char *stream_member(struct package *p, member_sink sink, void *ctx, char hex[SHA256_HEX])
{
	struct sha256_stream hash = { 0 };
	unsigned char *buf = malloc(CHUNK);
	const char *error = !buf || open_sha256_stream(&hash) ? "io" : NULL;
	ssize_t n = 0;
	while (!error && (n = read_tar_data(&p->tar, buf, CHUNK)) > 0)
		if (update_sha256_stream(&hash, buf, (size_t)n) || (sink && sink(ctx, buf, (size_t)n)))
			error = "io";
	error = error ? error : n < 0 ? "bad_package" : NULL;
	if (!error && finish_sha256_stream(&hash, hex))
		error = "io";
	close_sha256_stream(&hash);
	free(buf);
	return error;
}

const char *read_package_member(struct package *p, const struct manifest_file **file, member_sink sink, void *ctx)
{
	struct tar_member m;
	char hex[SHA256_HEX];
	if (p->next >= p->manifest.count)
		return "bad_package";
	*file = &p->manifest.files[p->next++];
	if (read_tar_member(&p->tar, &m) || strcmp(m.name, (*file)->name) || m.size != (*file)->size)
		return "bad_package";
	const char *error = stream_member(p, sink, ctx, hex);
	if (error)
		return error;
	return strcmp(hex, (*file)->sha256) ? "bad_package" : NULL;
}

const char *finish_package(struct package *p)
{
	struct tar_member m;
	return p->next == p->manifest.count && read_tar_member(&p->tar, &m) == 1 ? NULL : "bad_package";
}

void close_package(struct package *p)
{
	free(p->json);
	p->json = NULL;
}

unsigned long long measure_package(const struct manifest *m)
{
	unsigned long long total = 0;
	for (int i = 0; i < m->count; i++)
		total += m->files[i].is_image ? m->files[i].image_size : m->files[i].size;
	return total;
}
