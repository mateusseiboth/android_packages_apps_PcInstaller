#include "menus.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grubcfg.h"
#include "util.h"

#define MAX_ENTRIES 32
#define ENTRY_MAX 8192
#define MENU_MAX (MAX_ENTRIES * ENTRY_MAX + 16384)

static int is_valid_id(const char *id)
{
	for (const char *c = id; *c; c++)
		if (!isalnum((unsigned char)*c))
			return 0;
	return *id != 0;
}

static void build_entry_path(char *out, size_t cap, const char *dir, const char *id)
{
	snprintf(out, cap, "%s/entries/%s.cfg", dir, id);
}

int save_menu_entry(const char *dir, const char *id, const char *entry)
{
	char path[PATH_MAX], entries[PATH_MAX];
	if (!is_valid_id(id))
		return -1;
	snprintf(entries, sizeof entries, "%s/entries", dir);
	build_entry_path(path, sizeof path, dir, id);
	return make_dirs(entries, 0755) || save_text_file(path, entry) ? -1 : 0;
}

int remove_menu_entry(const char *dir, const char *id)
{
	char path[PATH_MAX];
	if (!is_valid_id(id))
		return -1;
	build_entry_path(path, sizeof path, dir, id);
	return unlink(path) && is_file(path) ? -1 : 0;
}

/* ---- listing -------------------------------------------------------------------------- */

static int is_entry_file(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && !strcmp(name + n - 4, ".cfg");
}

static int compare_names(const void *a, const void *b) { return strcmp(a, b); }

/* Entry ids, sorted, so the menu order does not depend on the directory. */
static int list_entry_ids(const char *dir, char ids[][64], int max)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/entries", dir);
	DIR *d = opendir(path);
	int count = 0;
	for (struct dirent *e; d && count < max && (e = readdir(d));) {
		if (!is_entry_file(e->d_name))
			continue;
		snprintf(ids[count], 64, "%.*s", (int)strlen(e->d_name) - 4, e->d_name);
		count += is_valid_id(ids[count]);
	}
	if (d)
		closedir(d);
	qsort(ids, (size_t)count, 64, compare_names);
	return count;
}

int count_menu_entries(const char *dir)
{
	char ids[MAX_ENTRIES][64];
	return list_entry_ids(dir, ids, MAX_ENTRIES);
}

/* ---- rebuilding ----------------------------------------------------------------------- */

static int read_entry(const char *dir, const char *id, char *out, size_t cap)
{
	char path[PATH_MAX];
	build_entry_path(path, sizeof path, dir, id);
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(out, 1, cap - 1, f) : 0;
	out[n] = 0;
	if (f)
		fclose(f);
	return f ? 0 : -1;
}

static const char *find_default(char ids[][64], int count, const char *wanted)
{
	for (int i = 0; wanted && i < count; i++)
		if (!strcmp(ids[i], wanted))
			return ids[i];
	return count ? ids[0] : "";
}

static int save_menu(const char *dir, const char *const *entries, int count, const char *others,
		     const char *default_id, int is_efi)
{
	char path[PATH_MAX], *menu = malloc(MENU_MAX);
	struct menu_params m = { entries, count, others, default_id, is_efi };
	snprintf(path, sizeof path, "%s/grub.cfg", dir);
	int failed = !menu || build_menu(menu, MENU_MAX, &m) || save_text_file(path, menu);
	free(menu);
	return failed ? -1 : 0;
}

int rebuild_menu(const char *dir, const char *others, const char *default_id, int is_efi)
{
	char ids[MAX_ENTRIES][64];
	int count = list_entry_ids(dir, ids, MAX_ENTRIES);
	char *texts = malloc((size_t)MAX_ENTRIES * ENTRY_MAX);
	const char *entries[MAX_ENTRIES];
	int failed = !texts;
	for (int i = 0; !failed && i < count; i++) {
		entries[i] = texts + (size_t)i * ENTRY_MAX;
		failed = read_entry(dir, ids[i], texts + (size_t)i * ENTRY_MAX, ENTRY_MAX);
	}
	failed = failed || save_menu(dir, entries, count, others, find_default(ids, count, default_id), is_efi);
	free(texts);
	return failed ? -1 : 0;
}
