#include "manifest.h"

#include <stddef.h>
#include <string.h>

#include "json.h"

#define MAX_TOKENS 160

struct doc {
	const char *text;
	struct json_token t[MAX_TOKENS];
};

/* ---- roles and types ------------------------------------------------------------------ */

static const struct {
	const char *role;
	int is_image;
} ROLES[] = { { "kernel", 0 }, { "initrd", 0 }, { "system", 1 }, { "overlay", 1 } };

#define ROLE_COUNT (sizeof ROLES / sizeof *ROLES)
#define KERNEL (1u << 0)
#define INITRD (1u << 1)
#define SYSTEM (1u << 2)
#define OVERLAY (1u << 3)

static int find_role(const char *role)
{
	for (size_t i = 0; i < ROLE_COUNT; i++)
		if (!strcmp(ROLES[i].role, role))
			return (int)i;
	return -1;
}

static int is_in(const char *word, const char *const *list)
{
	for (; *list; list++)
		if (!strcmp(word, *list))
			return 1;
	return 0;
}

static int is_valid_update(const struct manifest *m)
{
	static const char *const FLAVORS[] = { "gapps", "microg", "none", NULL };
	return is_in(m->flavor, FLAVORS);
}

int is_valid_patch_name(const char *name)
{
	for (const char *c = name; *c; c++)
		if (!strchr("abcdefghijklmnopqrstuvwxyz0123456789-", *c))
			return 0;
	return *name && strlen(name) < 64;
}

static int is_valid_patch(const struct manifest *m) { return is_valid_patch_name(m->name) && m->count > 0; }

/* An update is exactly a kernel, an initrd and a system; a patch any of an overlay, a kernel and an initrd. */
static const struct {
	const char *type;
	unsigned required, allowed;
	int (*is_valid)(const struct manifest *);
} TYPES[] = {
	{ "update", KERNEL | INITRD | SYSTEM, KERNEL | INITRD | SYSTEM, is_valid_update },
	{ "patch", 0, OVERLAY | KERNEL | INITRD, is_valid_patch },
};

/* ---- fields --------------------------------------------------------------------------- */

#define TEXT(field, required) { #field, offsetof(struct manifest, field), sizeof ((struct manifest *)0)->field, required }

static const struct {
	const char *key;
	size_t offset, cap;
	int required;
} TEXTS[] = {
	TEXT(type, 1), TEXT(version, 1), TEXT(build, 1),	 TEXT(name, 0),
	TEXT(description, 0), TEXT(flavor, 0), TEXT(fingerprint, 0),
};

/* A missing optional field reads as empty. */
static int copy_text(const struct doc *d, int obj, const char *key, char *out, size_t cap, int required)
{
	int v = find_json_key(d->text, d->t, obj, key);
	out[0] = 0;
	if (v < 0)
		return required ? -1 : 0;
	return copy_json_string(d->text, d->t, v, out, cap);
}

static int is_sha256(const char *hex)
{
	size_t n = strspn(hex, "0123456789abcdef");
	return n == 64 && !hex[n];
}

static int is_digits(const char *s) { return *s && !s[strspn(s, "0123456789")]; }

static int read_u64_key(const struct doc *d, int obj, const char *key, unsigned long long *out)
{
	return read_json_u64(d->text, d->t, find_json_key(d->text, d->t, obj, key), out);
}

static int read_image_fields(const struct doc *d, int obj, struct manifest_file *f)
{
	return read_u64_key(d, obj, "image_size", &f->image_size) ||
	       copy_text(d, obj, "image_sha256", f->image_sha256, sizeof f->image_sha256, 1) ||
	       !is_sha256(f->image_sha256) || !f->image_size;
}

static int read_file(const struct doc *d, int obj, struct manifest_file *f, unsigned *roles)
{
	*f = (struct manifest_file){ 0 };
	if (copy_text(d, obj, "name", f->name, sizeof f->name, 1) || !*f->name ||
	    copy_text(d, obj, "role", f->role, sizeof f->role, 1) || read_u64_key(d, obj, "size", &f->size) ||
	    copy_text(d, obj, "sha256", f->sha256, sizeof f->sha256, 1) || !is_sha256(f->sha256))
		return -1;
	int role = find_role(f->role);
	if (role < 0 || *roles & 1u << role)
		return -1;
	*roles |= 1u << role;
	f->is_image = ROLES[role].is_image;
	return f->is_image ? read_image_fields(d, obj, f) : 0;
}

static int has_unique_names(const struct manifest *m)
{
	for (int i = 0; i < m->count; i++)
		for (int j = i + 1; j < m->count; j++)
			if (!strcmp(m->files[i].name, m->files[j].name))
				return 0;
	return 1;
}

static int read_files(const struct doc *d, struct manifest *m, unsigned *roles)
{
	int arr = find_json_key(d->text, d->t, 0, "files");
	if (arr < 0 || d->t[arr].type != JSON_ARRAY || d->t[arr].count > MANIFEST_MAX_FILES)
		return -1;
	m->count = d->t[arr].count;
	for (int i = 0; i < m->count; i++)
		if (read_file(d, find_json_item(d->t, arr, i), &m->files[i], roles))
			return -1;
	return has_unique_names(m) ? 0 : -1;
}

static int read_texts(const struct doc *d, struct manifest *m)
{
	for (size_t i = 0; i < sizeof TEXTS / sizeof *TEXTS; i++)
		if (copy_text(d, 0, TEXTS[i].key, (char *)m + TEXTS[i].offset, TEXTS[i].cap, TEXTS[i].required))
			return -1;
	return 0;
}

/* patches/NAME.json adds these; a package's manifest has neither. */
static void read_replaced(const struct doc *d, struct manifest *m)
{
	read_json_bool(d->text, d->t, find_json_key(d->text, d->t, 0, "replaced_kernel"), &m->replaced_kernel);
	read_json_bool(d->text, d->t, find_json_key(d->text, d->t, 0, "replaced_initrd"), &m->replaced_initrd);
}

static int check_type(const struct manifest *m, unsigned roles)
{
	for (size_t i = 0; i < sizeof TYPES / sizeof *TYPES; i++)
		if (!strcmp(TYPES[i].type, m->type))
			return (roles & TYPES[i].required) == TYPES[i].required && !(roles & ~TYPES[i].allowed) &&
					       TYPES[i].is_valid(m)
				       ? 0
				       : -1;
	return -1;
}

int parse_manifest(const char *json, size_t len, struct manifest *out)
{
	static struct doc d;
	unsigned long long format = 0;
	unsigned roles = 0;
	*out = (struct manifest){ 0 };
	d.text = json;
	if (parse_json(json, len, d.t, MAX_TOKENS) < 0 || d.t[0].type != JSON_OBJECT)
		return -1;
	if (read_u64_key(&d, 0, "format", &format) || format != MANIFEST_FORMAT || read_texts(&d, out) ||
	    !is_digits(out->build) || read_files(&d, out, &roles))
		return -1;
	read_replaced(&d, out);
	return check_type(out, roles);
}

const struct manifest_file *find_manifest_file(const struct manifest *m, const char *role)
{
	for (int i = 0; i < m->count; i++)
		if (!strcmp(m->files[i].role, role))
			return &m->files[i];
	return NULL;
}
