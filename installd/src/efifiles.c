#include "efifiles.h"

#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "treecopy.h"
#include "util.h"

struct copy {
	const char *from; /* in the live source folder */
	const char *to;
};

static const struct copy MENU_FILES[] = {
	{ "shimx64.efi", "shimx64.efi" },
	{ "mmx64.efi", "mmx64.efi" },
	{ "grubx64.efi", "grubx64.efi" },
	{ "ENROLL_THIS_KEY_IN_MOKMANAGER.der", "ENROLL_THIS_KEY_IN_MOKMANAGER.der" },
};

static const struct copy FALLBACK_FILES[] = {
	{ "shimx64.efi", "BOOTX64.EFI" },
	{ "grubx64.efi", "grubx64.efi" },
	{ "mmx64.efi", "mmx64.efi" },
};

#define COUNT(a) (sizeof a / sizeof *a)

static const char *build_path(const char *dir, const char *sub, const char *name)
{
	static char out[4][PATH_MAX];
	static int next;
	char *p = out[next++ % 4];
	snprintf(p, PATH_MAX, "%s/%s/%s", dir, sub, name);
	return p;
}

static int copy_all(const char *src_dir, const char *esp, const char *sub, const struct copy *files, size_t count)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/%s", esp, sub);
	if (make_dirs(dir, 0755))
		return -1;
	for (size_t i = 0; i < count; i++) {
		char from[PATH_MAX];
		snprintf(from, sizeof from, "%s/%s", src_dir, files[i].from);
		if (copy_file(from, build_path(esp, sub, files[i].to), NULL, NULL))
			return -1;
	}
	return 0;
}

static int is_same_file(const char *a, const char *b)
{
	FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
	int same = fa && fb;
	for (int ca = 0, cb = 0; same && ca != EOF; same = ca == cb) {
		ca = fgetc(fa);
		cb = fgetc(fb);
	}
	if (fa)
		fclose(fa);
	if (fb)
		fclose(fb);
	return same;
}

int save_efi_files(const char *src_dir, const char *esp)
{
	return copy_all(src_dir, esp, EFI_MENU_DIR, MENU_FILES, COUNT(MENU_FILES));
}

/* Never by shim alone: other systems ship the very same signed shim. */
int is_our_fallback(const char *esp)
{
	const char *ours = build_path(esp, EFI_MENU_DIR, "grubx64.efi");
	return is_same_file(build_path(esp, EFI_FALLBACK_DIR, "BOOTX64.EFI"), ours) ||
	       is_same_file(build_path(esp, EFI_FALLBACK_DIR, "grubx64.efi"), ours);
}

int is_fallback_free(const char *esp)
{
	struct stat st;
	return lstat(build_path(esp, EFI_FALLBACK_DIR, "BOOTX64.EFI"), &st) || is_our_fallback(esp);
}

int save_fallback_files(const char *src_dir, const char *esp)
{
	return copy_all(src_dir, esp, EFI_FALLBACK_DIR, FALLBACK_FILES, COUNT(FALLBACK_FILES));
}

void remove_fallback_files(const char *esp)
{
	char dir[PATH_MAX];
	for (size_t i = 0; i < COUNT(FALLBACK_FILES); i++)
		unlink(build_path(esp, EFI_FALLBACK_DIR, FALLBACK_FILES[i].to));
	snprintf(dir, sizeof dir, "%s/%s", esp, EFI_FALLBACK_DIR);
	rmdir(dir);
}
