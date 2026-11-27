#include "pkgsig.h"

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <stdio.h>

static EVP_PKEY *read_rsa_key(const unsigned char *cert, size_t cert_len)
{
	const unsigned char *p = cert;
	X509 *x = d2i_X509(NULL, &p, (long)cert_len);
	EVP_PKEY *key = x ? X509_get_pubkey(x) : NULL;
	X509_free(x);
	if (key && EVP_PKEY_id(key) != EVP_PKEY_RSA) {
		EVP_PKEY_free(key);
		key = NULL;
	}
	return key;
}

/* RSA keys verify PKCS#1 v1.5 padding unless told otherwise. */
static int is_signed_by(EVP_PKEY *key, const void *data, size_t len, const unsigned char *sig, size_t sig_len)
{
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	int good = ctx && EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, key) == 1 &&
		   EVP_DigestVerifyUpdate(ctx, data, len) == 1 && EVP_DigestVerifyFinal(ctx, sig, sig_len) == 1;
	EVP_MD_CTX_free(ctx);
	return good;
}

enum signature_result verify_signature(const unsigned char *cert, size_t cert_len, const void *data, size_t len,
				       const unsigned char *sig, size_t sig_len)
{
	EVP_PKEY *key = read_rsa_key(cert, cert_len);
	if (!key)
		return SIGNATURE_NO_KEY;
	int good = is_signed_by(key, data, len, sig, sig_len);
	EVP_PKEY_free(key);
	return good ? SIGNATURE_GOOD : SIGNATURE_BAD;
}

/* ---- streaming sha256 ----------------------------------------------------------------- */

int open_sha256_stream(struct sha256_stream *s)
{
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1) {
		s->ctx = ctx;
		return 0;
	}
	EVP_MD_CTX_free(ctx);
	s->ctx = NULL;
	return -1;
}

int update_sha256_stream(struct sha256_stream *s, const void *data, size_t len)
{
	return EVP_DigestUpdate(s->ctx, data, len) == 1 ? 0 : -1;
}

int finish_sha256_stream(struct sha256_stream *s, char hex[SHA256_HEX])
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned len = 0;
	int failed = EVP_DigestFinal_ex(s->ctx, digest, &len) != 1 || len != 32;
	close_sha256_stream(s);
	for (unsigned i = 0; !failed && i < len; i++)
		snprintf(hex + 2 * i, 3, "%02x", digest[i]);
	return failed ? -1 : 0;
}

void close_sha256_stream(struct sha256_stream *s)
{
	EVP_MD_CTX_free(s->ctx);
	s->ctx = NULL;
}
