/*
 * GRUB for BIOS: boot.img in the disk's first sector loads core.img, which sits in a GPT BIOS boot
 * partition or in the free sectors before the first partition (what grub-bios-setup does).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "parttable.h"

/* 0 with *start set, or -1 when no place fits sectors (or the disk's sectors are not 512 bytes). */
int find_core_place(const struct part_table *t, unsigned lbs, uint64_t sectors, uint64_t *start);
/* boot.img with the old sector's BPB area, disk id and partition table, pointed at core_lba. */
void build_boot_sector(const uint8_t old[512], const uint8_t boot[512], uint64_t core_lba, uint8_t out[512]);
/* core.img's first sector loads the rest from core_lba + 1. */
void apply_core_blocklist(uint8_t *core, size_t len, uint64_t core_lba);
/* Saves the old first sector to backup (once), then writes core.img and boot.img. */
int write_bios_boot(const char *disk, const char *boot_img, const char *core_img, uint64_t core_lba,
		    const char *backup);
/* Puts the saved boot code back (the partition table is left as it is now). */
int restore_bios_boot(const char *disk, const char *backup);
