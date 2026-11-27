#include "layout.h"

#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

#include "grubcfg.h"
#include "treecopy.h"

static const char *build_in(const char *dir, const char *name, const char *suffix)
{
	static char out[4][PATH_MAX];
	static int next;
	char *p = out[next++ % 4];
	snprintf(p, PATH_MAX, "%s/%s%s", dir, name, suffix);
	return p;
}

static int exists(const char *path)
{
	struct stat st;
	return !lstat(path, &st);
}

/* A staged update counts: after the next boot pc-init has made it the system and this one system.old. */
static const struct {
	const char *name;
	int flag;
} PREVIOUS_FILES[] = {
	{ "system.old.img", PREVIOUS_SYSTEM }, { "system.old", PREVIOUS_SYSTEM },
	{ SYSTEM_NEW_IMAGE, PREVIOUS_SYSTEM }, { SYSTEM_NEW_DIR, PREVIOUS_SYSTEM },
	{ KERNEL_OLD, PREVIOUS_KERNEL },	{ INITRD_OLD, PREVIOUS_INITRD },
};

int read_previous_flags(const char *dir)
{
	int flags = 0;
	for (size_t i = 0; i < sizeof PREVIOUS_FILES / sizeof *PREVIOUS_FILES; i++)
		flags |= exists(build_in(dir, PREVIOUS_FILES[i].name, "")) ? PREVIOUS_FILES[i].flag : 0;
	return flags;
}

int swap_in_file(const char *dir, const char *name, const char *old_name)
{
	const char *path = build_in(dir, name, "");
	if (old_name && (remove_tree(build_in(dir, old_name, "")) ||
			 (exists(path) && rename(path, build_in(dir, old_name, "")))))
		return -1;
	return rename(build_in(dir, name, ".tmp"), path) ? -1 : 0;
}

int restore_old_file(const char *dir, const char *name, const char *old_name)
{
	const char *old = build_in(dir, old_name, "");
	return exists(old) && rename(old, build_in(dir, name, "")) ? -1 : 0;
}

static const char *const STAGED[] = {
	SYSTEM_NEW_IMAGE, SYSTEM_NEW_IMAGE ".tmp", SYSTEM_NEW_DIR, SYSTEM_NEW_DIR ".tmp", KERNEL_FILE ".tmp",
	INITRD_FILE ".tmp", UPDATE_LOCALE,	   UPDATE_LOCALE ".tmp",
};

int remove_staged_update(const char *dir)
{
	int failed = 0;
	for (size_t i = 0; i < sizeof STAGED / sizeof *STAGED; i++)
		failed |= remove_tree(build_in(dir, STAGED[i], ""));
	return failed ? -1 : 0;
}

int remove_patches(const char *dir) { return remove_tree(build_in(dir, PATCHES_DIR, "")); }
