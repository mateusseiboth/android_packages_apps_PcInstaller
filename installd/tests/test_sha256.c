#include <stdint.h>
#include <stdlib.h>

#include "check.h"
#include "sha256.h"

static void format_hex(const uint8_t digest[SHA256_SIZE], char out[2 * SHA256_SIZE + 1])
{
	for (int i = 0; i < SHA256_SIZE; i++)
		snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static const char *hash_hex(const void *data, size_t len)
{
	static char out[2 * SHA256_SIZE + 1];
	uint8_t digest[SHA256_SIZE];
	hash_sha256(data, len, digest);
	format_hex(digest, out);
	return out;
}

/* FIPS 180-2, appendix B. */
static void matches_the_fips_vectors(void)
{
	CHECK_STR(hash_hex("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_STR(hash_hex("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	const char *two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	CHECK_STR(hash_hex(two_blocks, strlen(two_blocks)),
		  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void a_million_a_in_uneven_pieces(void)
{
	char *a = malloc(1000000);
	memset(a, 'a', 1000000);
	struct sha256 ctx;
	uint8_t digest[SHA256_SIZE];
	char hex[2 * SHA256_SIZE + 1];
	init_sha256(&ctx);
	for (size_t done = 0, step = 1; done < 1000000; done += step, step = step * 3 % 997 + 1)
		update_sha256(&ctx, a + done, done + step > 1000000 ? 1000000 - done : step);
	finish_sha256(&ctx, digest);
	format_hex(digest, hex);
	CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	free(a);
}

int main(void)
{
	RUN(matches_the_fips_vectors);
	RUN(a_million_a_in_uneven_pieces);
	return CHECK_DONE();
}
