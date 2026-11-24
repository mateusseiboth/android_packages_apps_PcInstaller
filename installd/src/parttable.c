#include "parttable.h"

#include <string.h>

/* C12A7328-F81F-11D2-BA4B-00A0C93EC93B as stored on disk. */
static const uint8_t ESP_TYPE[16] = { 0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
				      0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b };
#define MBR_ESP 0xef
#define MBR_GPT_PROTECTIVE 0xee

static uint64_t read_le(const uint8_t *p, size_t n)
{
	uint64_t v = 0;
	while (n--)
		v = v << 8 | p[n];
	return v;
}

static int is_zero(const uint8_t *p, size_t n)
{
	while (n--)
		if (p[n])
			return 0;
	return 1;
}

static int parse_gpt(const uint8_t *head, size_t len, unsigned lbs, struct part_table *out)
{
	const uint8_t *hdr = head + lbs;
	if ((size_t)lbs + 92 > len || memcmp(hdr, "EFI PART", 8))
		return -1;
	uint64_t first = read_le(hdr + 72, 8) * lbs;
	uint32_t count = (uint32_t)read_le(hdr + 80, 4), size = (uint32_t)read_le(hdr + 84, 4);
	out->is_gpt = 1;
	for (uint32_t i = 0; i < count && out->count < PARTTABLE_MAX && first + (i + 1) * (uint64_t)size <= len; i++) {
		const uint8_t *e = head + first + i * (uint64_t)size;
		if (is_zero(e, 16))
			continue;
		struct part_entry *p = &out->entries[out->count++];
		*p = (struct part_entry){ .number = (int)i + 1, .start = read_le(e + 32, 8),
					  .size = read_le(e + 40, 8) - read_le(e + 32, 8) + 1,
					  .is_esp = !memcmp(e, ESP_TYPE, 16) };
		memcpy(p->unique, e + 16, 16);
	}
	return 0;
}

static int parse_mbr(const uint8_t *head, size_t len, struct part_table *out)
{
	if (len < 512 || head[510] != 0x55 || head[511] != 0xaa)
		return -1;
	memcpy(out->mbr_id, head + 0x1b8, 4);
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = head + 0x1be + 16 * i;
		if (!e[4])
			continue;
		out->entries[out->count++] = (struct part_entry){ .number = i + 1, .start = read_le(e + 8, 4),
								  .size = read_le(e + 12, 4), .is_esp = e[4] == MBR_ESP };
	}
	return 0;
}

static int is_protective_mbr(const uint8_t *head, size_t len) { return len >= 512 && head[0x1be + 4] == MBR_GPT_PROTECTIVE; }

int parse_part_table(const uint8_t *head, size_t len, unsigned lbs, struct part_table *out)
{
	*out = (struct part_table){ 0 };
	if (is_protective_mbr(head, len))
		return parse_gpt(head, len, lbs, out);
	return parse_mbr(head, len, out);
}

static void fill_gpt_signature(const struct part_table *t, const struct part_entry *e, struct efi_disk_location *out)
{
	(void)t;
	memcpy(out->signature, e->unique, 16);
	out->mbr_type = 2;
	out->signature_type = 2;
}

static void fill_mbr_signature(const struct part_table *t, const struct part_entry *e, struct efi_disk_location *out)
{
	(void)e;
	memcpy(out->signature, t->mbr_id, 4);
	out->mbr_type = 1;
	out->signature_type = 1;
}

int find_efi_location(const struct part_table *t, int number, struct efi_disk_location *out)
{
	static void (*const FILL[])(const struct part_table *, const struct part_entry *, struct efi_disk_location *) = {
		fill_mbr_signature, fill_gpt_signature
	};
	for (int i = 0; i < t->count; i++) {
		const struct part_entry *e = &t->entries[i];
		if (e->number != number)
			continue;
		*out = (struct efi_disk_location){ .number = (uint32_t)number, .start = e->start, .size = e->size };
		FILL[t->is_gpt](t, e, out);
		return 0;
	}
	return -1;
}
