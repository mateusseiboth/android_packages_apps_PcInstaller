#include "sha256.h"

#include <string.h>

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotate_right(uint32_t x, int n) { return x >> n | x << (32 - n); }

static uint32_t read_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void build_schedule(const uint8_t *block, uint32_t w[64])
{
	for (int i = 0; i < 16; i++)
		w[i] = read_be32(block + 4 * i);
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ w[i - 15] >> 3;
		uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ w[i - 2] >> 10;
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
}

static void compress_block(uint32_t state[8], const uint8_t *block)
{
	uint32_t w[64], v[8];
	build_schedule(block, w);
	memcpy(v, state, sizeof v);
	for (int i = 0; i < 64; i++) {
		uint32_t s1 = rotate_right(v[4], 6) ^ rotate_right(v[4], 11) ^ rotate_right(v[4], 25);
		uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
		uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
		uint32_t s0 = rotate_right(v[0], 2) ^ rotate_right(v[0], 13) ^ rotate_right(v[0], 22);
		uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
		memmove(v + 1, v, 7 * sizeof *v);
		v[4] += t1;
		v[0] = t1 + s0 + maj;
	}
	for (int i = 0; i < 8; i++)
		state[i] += v[i];
}

void init_sha256(struct sha256 *ctx)
{
	static const uint32_t H0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
					0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memset(ctx, 0, sizeof *ctx);
	memcpy(ctx->state, H0, sizeof H0);
}

void update_sha256(struct sha256 *ctx, const void *data, size_t len)
{
	const uint8_t *p = data;
	ctx->bytes += len;
	while (len) {
		size_t take = sizeof ctx->block - ctx->used < len ? sizeof ctx->block - ctx->used : len;
		memcpy(ctx->block + ctx->used, p, take);
		ctx->used += take;
		p += take;
		len -= take;
		if (ctx->used < sizeof ctx->block)
			continue;
		compress_block(ctx->state, ctx->block);
		ctx->used = 0;
	}
}

void finish_sha256(struct sha256 *ctx, uint8_t out[SHA256_SIZE])
{
	uint64_t bits = ctx->bytes * 8;
	uint8_t pad[72] = { 0x80 };
	size_t pad_len = (ctx->used < 56 ? 56 : 120) - ctx->used;
	for (int i = 0; i < 8; i++)
		pad[pad_len + i] = (uint8_t)(bits >> (56 - 8 * i));
	update_sha256(ctx, pad, pad_len + 8);
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 4; j++)
			out[4 * i + j] = (uint8_t)(ctx->state[i] >> (24 - 8 * j));
}

void hash_sha256(const void *data, size_t len, uint8_t out[SHA256_SIZE])
{
	struct sha256 ctx;
	init_sha256(&ctx);
	update_sha256(&ctx, data, len);
	finish_sha256(&ctx, out);
}
