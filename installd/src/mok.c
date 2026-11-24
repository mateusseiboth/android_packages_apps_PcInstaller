#include "mok.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efiboot.h"

#define LIST_HEADER 28 /* SignatureType, ListSize, HeaderSize, SignatureSize */
#define OWNER_SIZE 16
#define MAX_MOKLIST_PARTS 64

/* EFI_CERT_X509_GUID a5c059a1-94e4-4aa7-87b5-ab155c2bf072 and SHIM_LOCK_GUID, as stored (UEFI byte order). */
static const uint8_t X509_TYPE[16] = { 0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
				       0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 };
static const uint8_t SHIM_OWNER[16] = { 0x50, 0xab, 0x5d, 0x60, 0x46, 0xe0, 0x00, 0x43,
					0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23 };

static void put_le32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> 8 * i);
}

static uint32_t read_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

size_t build_cert_list(uint8_t *out, size_t cap, const uint8_t *der, size_t der_len)
{
	size_t total = LIST_HEADER + OWNER_SIZE + der_len;
	if (total > cap || total > UINT32_MAX)
		return 0;
	memcpy(out, X509_TYPE, 16);
	put_le32(out + 16, (uint32_t)total);
	put_le32(out + 20, 0);
	put_le32(out + 24, (uint32_t)(OWNER_SIZE + der_len));
	memcpy(out + LIST_HEADER, SHIM_OWNER, OWNER_SIZE);
	memcpy(out + LIST_HEADER + OWNER_SIZE, der, der_len);
	return total;
}

/* One list: its entries are SignatureSize each (owner + data), after HeaderSize bytes of header. */
static int has_cert_in_list(const uint8_t *list, size_t size, const uint8_t *der, size_t der_len)
{
	size_t header = read_le32(list + 20), entry = read_le32(list + 24);
	if (memcmp(list, X509_TYPE, 16) || entry != OWNER_SIZE + der_len || LIST_HEADER + header > size)
		return 0;
	for (size_t at = LIST_HEADER + header; at + entry <= size; at += entry)
		if (!memcmp(list + at + OWNER_SIZE, der, der_len))
			return 1;
	return 0;
}

int has_cert(const uint8_t *lists, size_t len, const uint8_t *der, size_t der_len)
{
	for (size_t at = 0; at + LIST_HEADER <= len;) {
		size_t size = read_le32(lists + at + 16);
		if (size < LIST_HEADER || size > len - at)
			return 0;
		if (has_cert_in_list(lists + at, size, der, der_len))
			return 1;
		at += size;
	}
	return 0;
}

void build_mok_auth(const uint8_t *list, size_t len, const char *password, uint8_t out[SHA256_SIZE])
{
	struct sha256 ctx;
	init_sha256(&ctx);
	update_sha256(&ctx, list, len);
	for (const char *c = password; *c; c++)
		update_sha256(&ctx, (const uint8_t[2]){ (uint8_t)*c, 0 }, 2);
	finish_sha256(&ctx, out);
}

void build_mok_password(uint64_t random, char out[MOK_PASSWORD_LEN + 1])
{
	snprintf(out, MOK_PASSWORD_LEN + 1, "%08llu", (unsigned long long)(random % 100000000ULL));
}

int build_mok_request(const uint8_t *der, size_t der_len, const uint8_t *existing, size_t existing_len,
		      const char *password, uint8_t **list, size_t *list_len, uint8_t auth[SHA256_SIZE])
{
	size_t ours = has_cert(existing, existing_len, der, der_len) ? 0 : LIST_HEADER + OWNER_SIZE + der_len;
	*list = malloc(existing_len + ours + 1);
	if (!*list)
		return -1;
	if (existing_len)
		memcpy(*list, existing, existing_len);
	*list_len = existing_len + (ours ? build_cert_list(*list + existing_len, ours, der, der_len) : 0);
	build_mok_auth(*list, *list_len, password, auth);
	return 0;
}

/* ---- efivarfs ------------------------------------------------------------------------- */

static int has_cert_in_var(const char *efivars, const char *guid, const char *name, const uint8_t *der,
			   size_t der_len, int *missing)
{
	uint8_t *data = NULL;
	size_t len = 0;
	*missing = load_vendor_var(efivars, guid, name, &data, &len) != 0;
	int found = !*missing && has_cert(data, len, der, der_len);
	free(data);
	return found;
}

/* shim mirrors a large MokList as MokListRT, MokListRT1, MokListRT2...; the first gap ends it. */
static int has_cert_in_moklist(const char *efivars, const uint8_t *der, size_t der_len)
{
	char name[32] = "MokListRT";
	int missing = 0;
	for (int i = 1; i <= MAX_MOKLIST_PARTS; i++) {
		if (has_cert_in_var(efivars, SHIM_LOCK_GUID, name, der, der_len, &missing))
			return 1;
		if (missing)
			return 0;
		snprintf(name, sizeof name, "MokListRT%d", i);
	}
	return 0;
}

int is_mok_enrolled(const char *efivars, const uint8_t *der, size_t der_len)
{
	int missing = 0;
	return has_cert_in_moklist(efivars, der, der_len) ||
	       has_cert_in_var(efivars, EFI_DB_GUID, "db", der, der_len, &missing);
}

int is_mok_queued(const char *efivars, const uint8_t *der, size_t der_len)
{
	int missing = 0;
	return has_cert_in_var(efivars, SHIM_LOCK_GUID, "MokNew", der, der_len, &missing);
}

int queue_mok_enrollment(const char *efivars, const uint8_t *der, size_t der_len, const char *password)
{
	uint8_t *existing = NULL, *list = NULL, auth[SHA256_SIZE];
	size_t existing_len = 0, list_len = 0;
	load_vendor_var(efivars, SHIM_LOCK_GUID, "MokNew", &existing, &existing_len);
	int failed = build_mok_request(der, der_len, existing, existing_len, password, &list, &list_len, auth) ||
		     save_vendor_var(efivars, SHIM_LOCK_GUID, "MokNew", list, list_len) ||
		     save_vendor_var(efivars, SHIM_LOCK_GUID, "MokAuth", auth, sizeof auth);
	free(existing);
	free(list);
	return failed ? -1 : 0;
}
