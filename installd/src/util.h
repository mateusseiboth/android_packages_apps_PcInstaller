/* Small file helpers shared by the sysfs and procfs readers. */
#pragma once
#include <stddef.h>

/* Reads a small text file without its trailing newline; -1 when missing. */
int read_text(const char *path, char *out, size_t cap);
int read_u64(const char *path, unsigned long long *out);
int is_dir(const char *path);
int is_file(const char *path);
/* Writes text to path.tmp, then renames it over path. */
int save_text_file(const char *path, const char *text);
/* mkdir -p */
int make_dirs(const char *path, unsigned mode);
