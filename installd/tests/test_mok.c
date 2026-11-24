#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "efiboot.h"
#include "mok.h"
#include "sha256.h"

/* A tiny stand-in for a DER certificate, and the list mokutil would build for it (computed with Python's
 * uuid.bytes_le and struct, independently of mok.c). */
static const uint8_t DER[] = { 0x30, 0x03, 0x02, 0x01, 0x05 };
static const uint8_t OTHER_DER[] = { 0x30, 0x01, 0x00 };
static const char DER_LIST_HEX[] = "a159c0a5e494a74a87b5ab155c2bf07231000000000000001500000050ab5d6046e00043abb63dd8"
				   "10dd8b233003020105";
/* sha256(list + "12345678" in UTF-16LE), and sha256(other list + list + "00000042" in UTF-16LE). */
static const char AUTH_HEX[] = "4f289ca3e7138b238bc634a476b1fa6589718dc0cecedd544c39aca090b8cab0";
static const char APPENDED_AUTH_HEX[] = "b2eb04eda14e0400002f7db6c5cc79688908bc57f2e7c6eae540dfe17dc4e0f4";

static const char *format_hex(const uint8_t *bytes, size_t len)
{
	static char out[512];
	for (size_t i = 0; i < len && 2 * i + 2 < sizeof out; i++)
		snprintf(out + 2 * i, 3, "%02x", bytes[i]);
	out[2 * len < sizeof out ? 2 * len : sizeof out - 1] = 0;
	return out;
}

static size_t build_list_of(const uint8_t *der, size_t len, uint8_t *out, size_t cap)
{
	return build_cert_list(out, cap, der, len);
}

static void encodes_the_certificate_list_like_mokutil(void)
{
	uint8_t list[128];
	size_t n = build_cert_list(list, sizeof list, DER, sizeof DER);
	CHECK(n == 28 + 16 + sizeof DER);
	CHECK_STR(format_hex(list, n), DER_LIST_HEX);
	CHECK(build_cert_list(list, 20, DER, sizeof DER) == 0);
}

static void finds_the_certificate_among_lists(void)
{
	uint8_t lists[256];
	size_t n = build_list_of(OTHER_DER, sizeof OTHER_DER, lists, sizeof lists);
	CHECK(!has_cert(lists, n, DER, sizeof DER));
	n += build_list_of(DER, sizeof DER, lists + n, sizeof lists - n);
	CHECK(has_cert(lists, n, DER, sizeof DER));
	CHECK(has_cert(lists, n, OTHER_DER, sizeof OTHER_DER));
	CHECK(!has_cert(lists, n - 1, DER, sizeof DER)); /* cut short: the last list does not count */
}

static void ignores_hashes_and_broken_lists(void)
{
	uint8_t lists[256];
	size_t n = build_list_of(DER, sizeof DER, lists, sizeof lists);
	lists[0] ^= 0xff; /* a SHA-256 list (or anything not X.509) holding the same bytes */
	CHECK(!has_cert(lists, n, DER, sizeof DER));
	lists[0] ^= 0xff;
	lists[16] = 0xff; /* ListSize past the end */
	CHECK(!has_cert(lists, n, DER, sizeof DER));
	n = build_list_of(DER, sizeof DER, lists, sizeof lists);
	memset(lists + 24, 0, 4); /* SignatureSize 0 must not loop forever */
	CHECK(!has_cert(lists, n, DER, sizeof DER));
	CHECK(!has_cert(lists, 10, DER, sizeof DER));
}

static void the_password_is_eight_digits(void)
{
	char password[MOK_PASSWORD_LEN + 1];
	build_mok_password(42, password);
	CHECK_STR(password, "00000042");
	build_mok_password(9123456789ULL, password);
	CHECK_STR(password, "23456789");
}

static void auth_is_the_hash_of_the_list_and_the_utf16_password(void)
{
	uint8_t list[128], auth[SHA256_SIZE];
	size_t n = build_cert_list(list, sizeof list, DER, sizeof DER);
	build_mok_auth(list, n, "12345678", auth);
	CHECK_STR(format_hex(auth, sizeof auth), AUTH_HEX);
}

static void a_request_adds_our_list_to_a_pending_one(void)
{
	uint8_t *list = NULL, auth[SHA256_SIZE], other[128];
	size_t len = 0;
	CHECK(!build_mok_request(DER, sizeof DER, NULL, 0, "12345678", &list, &len, auth));
	CHECK_STR(format_hex(list, len), DER_LIST_HEX);
	CHECK_STR(format_hex(auth, sizeof auth), AUTH_HEX);
	free(list);

	size_t other_len = build_list_of(OTHER_DER, sizeof OTHER_DER, other, sizeof other);
	CHECK(!build_mok_request(DER, sizeof DER, other, other_len, "00000042", &list, &len, auth));
	CHECK(len == other_len + 28 + 16 + sizeof DER && !memcmp(list, other, other_len));
	CHECK_STR(format_hex(auth, sizeof auth), APPENDED_AUTH_HEX);

	/* Asking again keeps the pending list as it is; only the password changes. */
	uint8_t *again = NULL;
	size_t again_len = 0;
	CHECK(!build_mok_request(DER, sizeof DER, list, len, "00000042", &again, &again_len, auth));
	CHECK(again_len == len && !memcmp(again, list, len));
	CHECK_STR(format_hex(auth, sizeof auth), APPENDED_AUTH_HEX);
	free(list);
	free(again);
}

/* ---- efivarfs, against a plain directory ---------------------------------------------- */

static char dir[64];

static void make_dir(void)
{
	snprintf(dir, sizeof dir, "/tmp/mokvars-XXXXXX");
	CHECK(mkdtemp(dir) != NULL);
}

static void remove_dir(void)
{
	DIR *d = opendir(dir);
	for (struct dirent *e; d && (e = readdir(d));)
		if (e->d_name[0] != '.')
			unlinkat(dirfd(d), e->d_name, 0);
	if (d)
		closedir(d);
	rmdir(dir);
}

static void put_list(const char *name, const char *guid, const uint8_t *der, size_t der_len, size_t filler)
{
	uint8_t *data = calloc(1, filler + 256);
	size_t n = 0;
	/* filler: a big X.509 list of another certificate, as a MokListRT with vendor certs would hold */
	uint8_t *big = calloc(1, filler + 1);
	if (filler)
		n = build_cert_list(data, filler + 256, big, filler);
	n += build_cert_list(data + n, filler + 256 - n, der, der_len);
	CHECK(!save_vendor_var(dir, guid, name, data, n));
	free(big);
	free(data);
}

static void reads_enrollment_from_moklistrt_and_db(void)
{
	make_dir();
	CHECK(!is_mok_enrolled(dir, DER, sizeof DER));
	put_list("MokListRT", SHIM_LOCK_GUID, OTHER_DER, sizeof OTHER_DER, 0);
	CHECK(!is_mok_enrolled(dir, DER, sizeof DER));
	put_list("MokListRT2", SHIM_LOCK_GUID, DER, sizeof DER, 0); /* not read: MokListRT1 is missing */
	CHECK(!is_mok_enrolled(dir, DER, sizeof DER));
	put_list("MokListRT1", SHIM_LOCK_GUID, DER, sizeof DER, 40000);
	CHECK(is_mok_enrolled(dir, DER, sizeof DER));
	remove_dir();

	make_dir();
	put_list("db", EFI_DB_GUID, DER, sizeof DER, 20000);
	CHECK(is_mok_enrolled(dir, DER, sizeof DER));
	remove_dir();
}

static void queues_the_enrollment_for_mokmanager(void)
{
	make_dir();
	CHECK(!is_mok_queued(dir, DER, sizeof DER));
	CHECK(!queue_mok_enrollment(dir, DER, sizeof DER, "12345678"));
	CHECK(is_mok_queued(dir, DER, sizeof DER));

	char path[128];
	uint8_t raw[256];
	snprintf(path, sizeof path, "%s/MokNew-" SHIM_LOCK_GUID, dir);
	int fd = open(path, O_RDONLY);
	ssize_t n = fd >= 0 ? read(fd, raw, sizeof raw) : -1;
	close(fd);
	CHECK(n == 4 + 28 + 16 + (ssize_t)sizeof DER && raw[0] == 7 && !raw[1] && !raw[2] && !raw[3]);
	CHECK_STR(format_hex(raw + 4, (size_t)n - 4), DER_LIST_HEX);

	snprintf(path, sizeof path, "%s/MokAuth-" SHIM_LOCK_GUID, dir);
	fd = open(path, O_RDONLY);
	n = fd >= 0 ? read(fd, raw, sizeof raw) : -1;
	close(fd);
	CHECK(n == 4 + SHA256_SIZE && raw[0] == 7);
	CHECK_STR(format_hex(raw + 4, SHA256_SIZE), AUTH_HEX);
	remove_dir();
}

static void queuing_keeps_another_pending_key(void)
{
	make_dir();
	put_list("MokNew", SHIM_LOCK_GUID, OTHER_DER, sizeof OTHER_DER, 0);
	CHECK(!queue_mok_enrollment(dir, DER, sizeof DER, "00000042"));
	uint8_t *data = NULL;
	size_t len = 0;
	CHECK(!load_vendor_var(dir, SHIM_LOCK_GUID, "MokNew", &data, &len));
	CHECK(data && has_cert(data, len, OTHER_DER, sizeof OTHER_DER) && has_cert(data, len, DER, sizeof DER));
	free(data);
	CHECK(!load_vendor_var(dir, SHIM_LOCK_GUID, "MokAuth", &data, &len));
	CHECK(len == SHA256_SIZE);
	CHECK_STR(format_hex(data, len), APPENDED_AUTH_HEX);
	free(data);
	remove_dir();
}

int main(void)
{
	RUN(encodes_the_certificate_list_like_mokutil);
	RUN(finds_the_certificate_among_lists);
	RUN(ignores_hashes_and_broken_lists);
	RUN(the_password_is_eight_digits);
	RUN(auth_is_the_hash_of_the_list_and_the_utf16_password);
	RUN(a_request_adds_our_list_to_a_pending_one);
	RUN(reads_enrollment_from_moklistrt_and_db);
	RUN(queues_the_enrollment_for_mokmanager);
	RUN(queuing_keeps_another_pending_key);
	return CHECK_DONE();
}
