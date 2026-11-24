/*
 * shim's Machine Owner Keys: whether a certificate is trusted (MokListRT, or the firmware's db), and
 * queuing its enrollment for MokManager on the next start, as `mokutil --import` does: MokNew holds
 * EFI_SIGNATURE_LISTs, MokAuth the SHA-256 of MokNew followed by the password in UTF-16LE.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#define SHIM_LOCK_GUID "605dab50-e046-4300-abb6-3dd810dd8b23"
#define EFI_DB_GUID "d719b2cb-3d3a-4596-a3bc-dad00e67656f"
#define MOK_PASSWORD_LEN 8

/* One EFI_SIGNATURE_LIST with der as an X.509 certificate owned by shim; bytes written, 0 when cap is short. */
size_t build_cert_list(uint8_t *out, size_t cap, const uint8_t *der, size_t der_len);
/* 1 when the signature lists hold der as an X.509 entry; malformed lists end the search. */
int has_cert(const uint8_t *lists, size_t len, const uint8_t *der, size_t der_len);
/* MokAuth: SHA-256 of list followed by password in UTF-16LE (no terminator). */
void build_mok_auth(const uint8_t *list, size_t len, const char *password, uint8_t out[SHA256_SIZE]);
/* 8 decimal digits (MokManager reads a US keyboard; digits type the same on every layout). */
void build_mok_password(uint64_t random, char out[MOK_PASSWORD_LEN + 1]);
/* The new MokNew (existing pending lists, plus der unless already there; malloc'd into *list) and its MokAuth. */
int build_mok_request(const uint8_t *der, size_t der_len, const uint8_t *existing, size_t existing_len,
		      const char *password, uint8_t **list, size_t *list_len, uint8_t auth[SHA256_SIZE]);

/* der is in MokListRT, MokListRT1, MokListRT2... or in db (efivars: an efivarfs directory). */
int is_mok_enrolled(const char *efivars, const uint8_t *der, size_t der_len);
/* A pending MokNew holds der. */
int is_mok_queued(const char *efivars, const uint8_t *der, size_t der_len);
/* Writes MokNew and MokAuth (NV|BS|RT under SHIM_LOCK_GUID). */
int queue_mok_enrollment(const char *efivars, const uint8_t *der, size_t der_len, const char *password);
