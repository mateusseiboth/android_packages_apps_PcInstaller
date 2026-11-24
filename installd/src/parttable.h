/* GPT and MBR partition tables: where the ESP is and how UEFI names it in a device path. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "efiboot.h"

/* Covers the GPT entry array at LBA 2 for 512- and 4096-byte sectors. */
#define PARTTABLE_HEAD_BYTES (64 * 1024)
#define PARTTABLE_MAX 128

struct part_entry {
	int number;
	uint64_t start, size; /* logical blocks */
	uint8_t unique[16];   /* GPT unique partition GUID, as stored */
	int is_esp;
	int is_bios_boot; /* GPT "BIOS boot" partition, for GRUB's core image */
};

struct part_table {
	int is_gpt;
	uint64_t first_usable; /* GPT: first sector after the partition entries */
	uint8_t mbr_id[4];
	int count;
	struct part_entry entries[PARTTABLE_MAX];
};

/* head: the disk's first PARTTABLE_HEAD_BYTES; lbs: its logical block size. */
int parse_part_table(const uint8_t *head, size_t len, unsigned lbs, struct part_table *out);
int find_efi_location(const struct part_table *t, int number, struct efi_disk_location *out);
