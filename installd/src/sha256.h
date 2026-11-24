/* SHA-256 (FIPS 180-4), for the MokAuth hash shim's MokManager checks. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define SHA256_SIZE 32

struct sha256 {
	uint32_t state[8];
	uint64_t bytes;
	uint8_t block[64];
	size_t used;
};

void init_sha256(struct sha256 *ctx);
void update_sha256(struct sha256 *ctx, const void *data, size_t len);
void finish_sha256(struct sha256 *ctx, uint8_t out[SHA256_SIZE]);
void hash_sha256(const void *data, size_t len, uint8_t out[SHA256_SIZE]);
