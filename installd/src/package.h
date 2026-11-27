/*
 * An update or patch package read from the file descriptor the app passes (docs/UPDATES.md in the device
 * tree): manifest.json, manifest.sig, then the files the manifest lists, each checked against its size
 * and sha256 as it streams by. Read once, front to back.
 */
#pragma once
#include <stddef.h>

#include "manifest.h"
#include "tar.h"

#define MANIFEST_MAX_BYTES (64 * 1024)

struct package {
	struct tar_reader tar;
	struct manifest manifest;
	char *json; /* manifest.json as signed, NUL-terminated */
	size_t json_len;
	int next;   /* the manifest file whose member comes next */
};

/* The member's bytes as they stream; -1 stops the read. */
typedef int (*member_sink)(void *ctx, const void *data, size_t len);

/* Reads the manifest and checks its signature with cert (DER): NULL, or bad_package, bad_signature,
 * no_certificate. */
const char *open_package(struct package *p, int fd, const unsigned char *cert, size_t cert_len);
/* Streams the next listed member to sink (NULL: just check it): NULL, bad_package, or io when sink
 * failed. *file gets the member's entry before the first byte. */
const char *read_package_member(struct package *p, const struct manifest_file **file, member_sink sink, void *ctx);
/* After the last member: bad_package when anything else follows. */
const char *finish_package(struct package *p);
void close_package(struct package *p);
/* What the package writes into the install folder: images decompressed, kernel and initrd. */
unsigned long long measure_package(const struct manifest *m);
