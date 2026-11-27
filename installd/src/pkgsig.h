/*
 * The package's signature and hashes, through libcrypto (BoringSSL on Android, OpenSSL on the host):
 * manifest.json is signed RSA-SHA256 (PKCS#1 v1.5) with the builder's Secure Boot key, and every
 * member is checked against the sha256 the signed manifest gives.
 */
#pragma once
#include <stddef.h>

#include "manifest.h"

enum signature_result { SIGNATURE_GOOD, SIGNATURE_BAD, SIGNATURE_NO_KEY };

/* cert: the builder's X.509 certificate (DER), ENROLL_THIS_KEY_IN_MOKMANAGER.der. */
enum signature_result verify_signature(const unsigned char *cert, size_t cert_len, const void *data, size_t len,
				       const unsigned char *sig, size_t sig_len);

struct sha256_stream {
	void *ctx;
};

int open_sha256_stream(struct sha256_stream *s);
int update_sha256_stream(struct sha256_stream *s, const void *data, size_t len);
/* Lower-case hex; also frees the stream. */
int finish_sha256_stream(struct sha256_stream *s, char hex[SHA256_HEX]);
void close_sha256_stream(struct sha256_stream *s);
