/*
 * Updates and patches for the running install, from a package (package.h). An update is staged as
 * system.new.img or system.new/ for pc-init to put in place on the next boot; a patch goes into patches/.
 * Both may bring a kernel and an initrd, which replace the install's and keep the ones they replace as
 * kernel.old and initrd.old.img for the previous system. Layout: device/pc/pc_x86_64/docs/UPDATES.md.
 */
#pragma once
#include "install.h"
#include "manifest.h"
#include "model.h"
#include "package.h"
#include "sysops.h"

struct apply_job {
	const struct sys_env *env;
	const struct sys_ops *ops;
	const struct partition *target; /* the running install's partition */
	const struct partition *parts;  /* every partition: the menus live there */
	int count;
	const char *folder;             /* pc.src */
	const char *id;                 /* pc.id, when the folder holds no id file */
	const char *locale;             /* update: the app's language for the boot screen; may be empty */
	const char *current_build;      /* the running system's ro.system.build.date.utc */
	const char *current_flavor;
};

/* pkg is open (signature checked). Errors: not_installed, wrong_flavor, wrong_base, no_room, bad_package,
 * io, mount_failed, menu_failed. Progress steps: prepare, kernel, system, boot, finish. */
int run_apply(const struct apply_job *job, struct package *pkg, install_progress progress, void *ctx,
	      const char **error);
/* Errors: bad_request (no such patch), io, mount_failed, menu_failed. */
int run_remove_patch(const struct apply_job *job, const char *name, const char **error);

typedef void (*patch_listener)(void *ctx, const struct manifest *patch);
/* Every patch in name order (patches/NAME.json); -1 when the install cannot be mounted. */
int list_patches(const struct apply_job *job, patch_listener each, void *ctx);
