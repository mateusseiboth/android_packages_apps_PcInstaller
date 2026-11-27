/*
 * manifest.json of an update or patch package (device/pc/pc_x86_64/docs/UPDATES.md): what it is, the
 * build it is or applies to, and the files that follow it in the tar.
 */
#pragma once
#include <stddef.h>

#define MANIFEST_FORMAT 1
#define MANIFEST_MAX_FILES 3
#define SHA256_HEX 65

struct manifest_file {
	char name[101];  /* member name in the tar */
	char role[16];   /* kernel, initrd, system, overlay */
	unsigned long long size;
	char sha256[SHA256_HEX];
	int is_image;    /* zstd of an image: image_size/image_sha256 describe it decompressed */
	unsigned long long image_size;
	char image_sha256[SHA256_HEX];
};

struct manifest {
	char type[16];   /* update or patch */
	char version[64];
	char name[64];   /* patch: [a-z0-9-]+, its file name in patches/ */
	char description[400];
	char flavor[16]; /* update: gapps, microg or none */
	char build[24];  /* ro.system.build.date.utc: of the update, or the only one a patch applies to */
	char fingerprint[256];
	struct manifest_file files[MANIFEST_MAX_FILES];
	int count;
	int replaced_kernel, replaced_initrd; /* patches/NAME.json only */
};

/* 0, or -1 when the text is not a manifest this daemon can apply. */
int parse_manifest(const char *json, size_t len, struct manifest *out);
const struct manifest_file *find_manifest_file(const struct manifest *m, const char *role);
int is_valid_patch_name(const char *name);
