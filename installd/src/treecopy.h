/*
 * Copies for the install: one file (keeping holes, for the images) and a whole tree (the system
 * image's files) with owners, modes, extended attributes (security.selinux, security.capability),
 * times, symlinks and hardlinks.
 */
#pragma once

typedef void (*copy_progress)(void *ctx, unsigned long long bytes);

int copy_file(const char *src, const char *dst, copy_progress progress, void *ctx);
int copy_tree(const char *src, const char *dst, copy_progress progress, void *ctx);
/* Bytes of the regular files under dir, hardlinked files once. */
unsigned long long measure_tree(const char *dir);
/* Removes a file or a directory tree; a missing path is not an error. */
int remove_tree(const char *path);
