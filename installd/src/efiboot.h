/*
 * A UEFI boot entry for GRUB on the ESP: the EFI_LOAD_OPTION (UEFI spec 3.1.3) with a hard drive +
 * file path device path, written as Boot#### through efivarfs and put first in BootOrder.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define EFI_GLOBAL_GUID "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define EFIVARS_DIR "/sys/firmware/efi/efivars"

/* The HARDDRIVE_DEVICE_PATH fields of the ESP (UEFI spec 10.3.5.1). */
struct efi_disk_location {
	uint32_t number;          /* partition number, from 1 */
	uint64_t start, size;     /* in logical blocks */
	uint8_t signature[16];    /* GPT unique partition GUID as stored on disk, or the MBR disk id */
	uint8_t mbr_type;         /* 1 MBR, 2 GPT */
	uint8_t signature_type;   /* 1 MBR 32-bit id, 2 GUID */
};

/* Bytes written to out, or 0 when cap is too small. path uses backslashes. */
size_t build_load_option(uint8_t *out, size_t cap, const char *description, const struct efi_disk_location *loc,
			 const char *path);
/* Moves (or inserts) number to the front of order; returns the new count. */
int apply_boot_first(uint16_t *order, int count, int max, uint16_t number);
int find_free_boot_number(const uint16_t *used, int count);

/* A variable of any vendor: <dir>/<name>-<guid>, 4 bytes of attributes (NV|BS|RT) then the data. */
int save_vendor_var(const char *dir, const char *guid, const char *name, const void *data, size_t len);
/* data gets a malloc'd copy without the attributes (free it); -1 when missing. */
int load_vendor_var(const char *dir, const char *guid, const char *name, uint8_t **data, size_t *len);
int save_efi_var(const char *dir, const char *name, const void *data, size_t len);
int read_efi_var(const char *dir, const char *name, void *data, size_t cap, size_t *len);
/* Reuses a Boot#### holding the same option, else takes a free number; then updates BootOrder. */
int apply_boot_entry(const char *dir, const uint8_t *option, size_t len, uint16_t *number);
/* Secure Boot on: the firmware starts only signed loaders (our shim is). */
int is_secure_boot_on(const char *dir);
/* Deletes every Boot#### whose description is description and drops it from BootOrder; returns how many. */
int remove_boot_entries(const char *dir, const char *description);
/* Deletes every Boot#### holding exactly option; returns how many. */
int remove_boot_option(const char *dir, const uint8_t *option, size_t len);
