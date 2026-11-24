#include "plan.h"

#include <ctype.h>
#include <string.h>

#define NO_LIMIT 0ULL
#define FAT32_LIMIT ((4ULL << 30) - 1)

static const struct fs_rule FS_RULES[] = {
	{ "ext4", NULL, 1, NO_LIMIT, "ext4" },
	{ "f2fs", NULL, 1, NO_LIMIT, "f2fs" },
	{ "btrfs", NULL, 1, NO_LIMIT, "btrfs" },
	{ "ntfs", NULL, 0, NO_LIMIT, "ntfs3" },
	{ "exfat", NULL, 0, NO_LIMIT, "exfat" },
	{ "vfat", NULL, 0, FAT32_LIMIT, "vfat" },
	{ "bitlocker", "bitlocker", 0, NO_LIMIT, NULL },
	{ "luks", "encrypted", 0, NO_LIMIT, NULL },
	{ "lvm", "lvm", 0, NO_LIMIT, NULL },
	{ "raid", "raid", 0, NO_LIMIT, NULL },
};

static const struct fs_rule UNSUPPORTED = { "unknown", "fs_unsupported", 0, NO_LIMIT, NULL };

const struct fs_rule *find_fs_rule(const char *type)
{
	for (size_t i = 0; i < sizeof FS_RULES / sizeof *FS_RULES; i++)
		if (!strcmp(FS_RULES[i].type, type))
			return &FS_RULES[i];
	return &UNSUPPORTED;
}

const char *find_mount_type(const char *fs)
{
	const char *type = find_fs_rule(fs)->mount_type;
	return type ? type : fs;
}

/* ---- folder name ---------------------------------------------------------------------- */

static int has_only_safe_chars(const char *f)
{
	for (; *f; f++)
		if (!isalnum((unsigned char)*f) && !strchr("._-/", *f))
			return 0;
	return 1;
}

static int has_no_empty_or_parent_component(const char *f)
{
	return !strstr(f, "//") && strcmp(f, "..") && strncmp(f, "../", 3) && !strstr(f, "/../") &&
	       !(strlen(f) >= 3 && !strcmp(f + strlen(f) - 3, "/.."));
}

static int is_relative_and_bounded(const char *f)
{
	size_t n = strlen(f);
	return n && n <= 64 && f[0] != '/' && f[n - 1] != '/';
}

int is_valid_folder(const char *folder)
{
	return is_relative_and_bounded(folder) && has_only_safe_chars(folder) &&
	       has_no_empty_or_parent_component(folder);
}

/* ---- checks, in the order the user should hear about them ----------------------------- */

struct plan_ctx {
	const struct plan_request *r;
	const struct partition *p;
	const struct source_sizes *src;
	const struct install_state *state;
	const struct machine *m;
	const struct fs_rule *rule;
	unsigned long long need;
};

static int is_one_of(const char *v, const char *const *set)
{
	for (; v && *set; set++)
		if (!strcmp(v, *set))
			return 1;
	return 0;
}

/* UEFI puts GRUB on an ESP; BIOS puts its core image in the gap before the first partition. */
static const char *check_boot(const struct plan_ctx *c)
{
	static const char *const MISSING[] = { "no_boot_room", "no_esp" };
	int has_room = c->m->is_uefi ? c->m->has_esp : c->m->has_boot_room;
	return has_room ? NULL : MISSING[c->m->is_uefi != 0];
}
static const char *check_folder(const struct plan_ctx *c) { return is_valid_folder(c->r->folder) ? NULL : "bad_folder"; }
static const char *check_source(const struct plan_ctx *c) { return c->p->is_source ? "source_partition" : NULL; }
static const char *check_fs(const struct plan_ctx *c) { return c->rule->reason; }

static const char *check_forms(const struct plan_ctx *c)
{
	static const char *const SYSTEM_FORMS[] = { "image", "folder", NULL };
	static const char *const DATA_FORMS[] = { "image", "folder", "keep", NULL };
	return is_one_of(c->r->system_form, SYSTEM_FORMS) && is_one_of(c->r->data_form, DATA_FORMS) ? NULL
												      : "bad_request";
}

static int wants_folder(const struct plan_ctx *c)
{
	return !strcmp(c->r->system_form, "folder") || !strcmp(c->r->data_form, "folder");
}

static const char *check_folders_allowed(const struct plan_ctx *c)
{
	return wants_folder(c) && !c->rule->allows_folders ? "fs_images_only" : NULL;
}

static int wants_data_image(const struct plan_ctx *c) { return !strcmp(c->r->data_form, "image"); }

static const char *check_existing_data(const struct plan_ctx *c)
{
	int keep = !strcmp(c->r->data_form, "keep");
	if (keep && !c->state->has_data)
		return "no_data_to_keep";
	return !keep && c->state->has_data ? "data_exists" : NULL;
}

static const char *check_data_size(const struct plan_ctx *c)
{
	return wants_data_image(c) && c->r->data_size < PLAN_MIN_DATA ? "data_too_small" : NULL;
}

static int is_over_limit(const struct plan_ctx *c, unsigned long long bytes)
{
	return c->rule->max_file != NO_LIMIT && bytes > c->rule->max_file;
}

static const char *check_file_limit(const struct plan_ctx *c)
{
	int system_too_big = !strcmp(c->r->system_form, "image") && is_over_limit(c, c->src->system);
	int data_too_big = wants_data_image(c) && is_over_limit(c, c->r->data_size);
	return system_too_big || data_too_big ? "file_too_large" : NULL;
}

static const char *check_space(const struct plan_ctx *c)
{
	return c->need > c->p->free + c->state->old_system_bytes ? "no_space" : NULL;
}

static const char *(*const CHECKS[])(const struct plan_ctx *) = {
	check_boot,	     check_folder,	check_source, check_fs,	  check_forms,
	check_folders_allowed, check_existing_data, check_data_size, check_file_limit, check_space,
};

static unsigned long long find_need(const struct plan_request *r, const struct source_sizes *src)
{
	unsigned long long data = !strcmp(r->data_form ? r->data_form : "", "image") ? r->data_size : 0;
	return src->kernel + src->initrd + src->system + data + PLAN_MARGIN;
}

int validate_plan(const struct plan_request *r, const struct partition *p, const struct source_sizes *src,
		  const struct install_state *state, const struct machine *m, struct plan_result *out)
{
	struct plan_ctx c = { r, p, src, state, m, find_fs_rule(p->fs), find_need(r, src) };
	*out = (struct plan_result){ .mode = state->has_system ? "update" : "install", .need = c.need };
	for (size_t i = 0; i < sizeof CHECKS / sizeof *CHECKS; i++) {
		out->error = CHECKS[i](&c);
		if (out->error)
			return -1;
	}
	return 0;
}
