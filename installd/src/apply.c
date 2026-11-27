#define _GNU_SOURCE
#include "apply.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "bootmenu.h"
#include "layout.h"
#include "plan.h"
#include "treecopy.h"
#include "unzstd.h"
#include "util.h"

#define PATCH_MARGIN (32ULL << 20)
#define BOOT_WEIGHT (8ULL << 20)
#define MAX_PATCHES 32
#define PATCH_JSON_EXTRA 128

struct apply_run {
	const struct apply_job *job;
	struct package *pkg;
	const struct manifest *m;
	install_progress progress;
	void *ctx;
	char target[PATH_MAX / 4], folder[PATH_MAX / 2], image[PATH_MAX / 4];
	int target_mounted;
	struct install_state state;
	int fd;               /* the member being written */
	struct unzstd z;
	int is_unzstd_open;
	const char *sink_error;
	int is_menu_changed;  /* kernel, initrd or system changed: the GRUB entry follows */
	const char *step, *reported_step;
	unsigned long long total, done;
	int percent;
};

/* ---- paths ---------------------------------------------------------------------------- */

static const char *in_folder(const struct apply_run *r, const char *name)
{
	static char out[4][PATH_MAX];
	static int next;
	char *p = out[next++ % 4];
	snprintf(p, PATH_MAX, "%s/%s", r->folder, name);
	return p;
}

/* patches/NAME<suffix> */
static const char *in_patches(const struct apply_run *r, const char *name, const char *suffix)
{
	char rel[PATH_MAX / 2];
	snprintf(rel, sizeof rel, PATCHES_DIR "/%s%s", name, suffix);
	return in_folder(r, rel);
}

static int exists(const char *path)
{
	struct stat st;
	return !lstat(path, &st);
}

static const char *find_io_error(void) { return errno == ENOSPC ? "no_room" : "io"; }

/* ---- progress ------------------------------------------------------------------------- */

static void report(struct apply_run *r)
{
	int percent = (int)(r->done * 100 / r->total);
	if (percent == r->percent && r->step == r->reported_step)
		return;
	r->percent = percent;
	r->reported_step = r->step;
	r->progress(r->ctx, r->step, percent);
}

static void enter_step(struct apply_run *r, const char *step)
{
	r->step = step;
	report(r);
}

static void add_done(struct apply_run *r, unsigned long long bytes)
{
	r->done = r->done + bytes < r->total ? r->done + bytes : r->total;
	report(r);
}

static void on_copied(void *ctx, unsigned long long bytes) { add_done(ctx, bytes); }

/* ---- the install folder --------------------------------------------------------------- */

static const char *open_folder(struct apply_run *r)
{
	const struct apply_job *job = r->job;
	char dev[PATH_MAX];
	snprintf(dev, sizeof dev, "%s/%s", job->env->dev_dir, job->target->name);
	make_dirs(r->target, 0700);
	/* Read-write like pc-init's mount of it: the kernel refuses a second mount with another mode. */
	if (job->ops->mount(job->env, dev, r->target, find_mount_type(job->target->fs), 0))
		return "mount_failed";
	r->target_mounted = 1;
	read_install_state(r->folder, &r->state);
	return r->state.has_system ? NULL : "not_installed";
}

static void close_folder(struct apply_run *r)
{
	if (r->target_mounted)
		r->job->ops->umount(r->job->env, r->target);
	r->target_mounted = 0;
}

/* The entry of this install in every GRUB menu, with the previous system's kernel when kept. */
static const char *save_entry(struct apply_run *r)
{
	const struct apply_job *job = r->job;
	const char *id = *r->state.id ? r->state.id : job->id;
	char grub[PATH_MAX];
	struct machine_scan *scan = r->is_menu_changed ? malloc(sizeof *scan) : NULL;
	if (!r->is_menu_changed)
		return NULL;
	if (!scan || !*id) {
		free(scan);
		return "menu_failed";
	}
	scan_machine(job->env, job->ops, job->parts, job->count, scan);
	/* The kernel refuses the read-only scan of the mounted install partition; BIOS keeps its menu there. */
	snprintf(grub, sizeof grub, "%s/%s", r->target, find_menu_dir(0));
	if (is_dir(grub))
		add_menu_place(scan, job->target, 0);
	int failed = apply_install_entry(job->env, job->ops, scan, job->target, job->folder, r->folder, id);
	free(scan);
	return failed ? "menu_failed" : NULL;
}

/* ---- patches/ ------------------------------------------------------------------------- */

static int is_json_name(const char *name)
{
	size_t n = strlen(name);
	return n > 5 && !strcmp(name + n - 5, ".json");
}

static int compare_names(const void *a, const void *b) { return strcmp(a, b); }

static int read_patch(const char *path, struct manifest *out)
{
	unsigned char *data = NULL;
	size_t len = 0;
	if (load_file(path, MANIFEST_MAX_BYTES + PATCH_JSON_EXTRA, &data, &len))
		return -1;
	int failed = parse_manifest((const char *)data, len, out);
	free(data);
	return failed;
}

/* Every readable patches/NAME.json, in name order. */
static void each_patch(const struct apply_run *r, patch_listener each, void *ctx)
{
	static char names[MAX_PATCHES][NAME_MAX + 1];
	static struct manifest patch;
	char path[PATH_MAX * 2];
	int count = 0;
	DIR *d = opendir(in_folder(r, PATCHES_DIR));
	for (struct dirent *e; d && count < MAX_PATCHES && (e = readdir(d));)
		if (is_json_name(e->d_name))
			snprintf(names[count++], NAME_MAX + 1, "%s", e->d_name);
	if (d)
		closedir(d);
	qsort(names, (size_t)count, sizeof *names, compare_names);
	for (int i = 0; i < count; i++) {
		snprintf(path, sizeof path, "%s/" PATCHES_DIR "/%.255s", r->folder, names[i]);
		if (!read_patch(path, &patch))
			each(ctx, &patch);
	}
}

struct replaced {
	int kernel, initrd;
};

static void add_replaced(void *ctx, const struct manifest *patch)
{
	struct replaced *rep = ctx;
	rep->kernel |= patch->replaced_kernel;
	rep->initrd |= patch->replaced_initrd;
}

static struct replaced find_replaced(const struct apply_run *r)
{
	struct replaced rep = { 0, 0 };
	each_patch(r, add_replaced, &rep);
	return rep;
}

/* The kernel and initrd kept from before the patches come back when no patch left replaced them. */
static int drop_patch(struct apply_run *r, const char *name)
{
	struct manifest patch = { 0 };
	read_patch(in_patches(r, name, ".json"), &patch);
	if (remove_tree(in_patches(r, name, ".img")) || remove_tree(in_patches(r, name, ".json")))
		return -1;
	struct replaced left = find_replaced(r);
	int restore_kernel = patch.replaced_kernel && !left.kernel, restore_initrd = patch.replaced_initrd && !left.initrd;
	r->is_menu_changed |= restore_kernel || restore_initrd;
	rmdir(in_folder(r, PATCHES_DIR));
	return (restore_kernel && restore_old_file(r->folder, KERNEL_FILE, KERNEL_OLD)) ||
			       (restore_initrd && restore_old_file(r->folder, INITRD_FILE, INITRD_OLD))
		       ? -1
		       : 0;
}

/* patches/NAME.json: the manifest as signed, plus what the patch replaced. */
static int save_patch_json(struct apply_run *r, int kernel, int initrd)
{
	const char *json = r->pkg->json;
	size_t end = (size_t)(strrchr(json, '}') - json), cap = r->pkg->json_len + PATCH_JSON_EXTRA;
	while (end && isspace((unsigned char)json[end - 1]))
		end--;
	char *text = malloc(cap);
	if (!text)
		return -1;
	snprintf(text, cap, "%.*s,\n  \"replaced_kernel\": %s,\n  \"replaced_initrd\": %s\n}\n", (int)end, json,
		 kernel ? "true" : "false", initrd ? "true" : "false");
	int failed = save_text_file(in_patches(r, r->m->name, ".json"), text);
	free(text);
	return failed;
}

/* ---- the members ---------------------------------------------------------------------- */

static void build_kernel_tmp(const struct apply_run *r, char *out, size_t cap)
{
	snprintf(out, cap, "%s", in_folder(r, KERNEL_FILE ".tmp"));
}

static void build_initrd_tmp(const struct apply_run *r, char *out, size_t cap)
{
	snprintf(out, cap, "%s", in_folder(r, INITRD_FILE ".tmp"));
}

static void build_system_tmp(const struct apply_run *r, char *out, size_t cap)
{
	snprintf(out, cap, "%s", in_folder(r, SYSTEM_NEW_IMAGE ".tmp"));
}

static void build_overlay_tmp(const struct apply_run *r, char *out, size_t cap)
{
	snprintf(out, cap, "%s", in_patches(r, r->m->name, ".img.tmp"));
}

static const struct {
	const char *role;
	const char *step;
	void (*build_tmp)(const struct apply_run *, char *, size_t);
} WRITERS[] = {
	{ "kernel", "kernel", build_kernel_tmp },
	{ "initrd", "kernel", build_initrd_tmp },
	{ "system", "system", build_system_tmp },
	{ "overlay", "system", build_overlay_tmp },
};

#define WRITER_COUNT (sizeof WRITERS / sizeof *WRITERS)

static size_t find_writer(const char *role)
{
	size_t i = 0;
	while (i < WRITER_COUNT - 1 && strcmp(WRITERS[i].role, role))
		i++;
	return i;
}

static void remove_tmp_files(const struct apply_run *r)
{
	char tmp[PATH_MAX];
	for (size_t i = 0; i < WRITER_COUNT; i++) {
		WRITERS[i].build_tmp(r, tmp, sizeof tmp);
		remove_tree(tmp);
	}
	remove_tree(in_folder(r, SYSTEM_NEW_DIR ".tmp"));
	rmdir(in_folder(r, PATCHES_DIR)); /* only when empty */
}

static int on_member_bytes(void *ctx, const void *data, size_t len)
{
	struct apply_run *r = ctx;
	errno = 0;
	int failed = r->is_unzstd_open ? write_unzstd(&r->z, data, len) : write_all(r->fd, data, len);
	if (failed) {
		r->sink_error = failed == UNZSTD_BAD_STREAM ? "bad_package" : find_io_error();
		return -1;
	}
	add_done(r, len);
	return 0;
}

/* The decompressed image must be the one the signed manifest describes. */
static const char *check_image(struct apply_run *r, const struct manifest_file *f)
{
	char hex[SHA256_HEX];
	unsigned long long size = 0;
	r->is_unzstd_open = 0;
	errno = 0;
	int failed = finish_unzstd(&r->z, hex, &size);
	if (failed)
		return failed == UNZSTD_BAD_STREAM ? "bad_package" : find_io_error();
	return size != f->image_size || strcmp(hex, f->image_sha256) ? "bad_package" : NULL;
}

static const char *stream_member(struct apply_run *r, const struct manifest_file *f)
{
	const struct manifest_file *read = NULL;
	if (f->is_image && open_unzstd(&r->z, r->fd))
		return "io";
	r->is_unzstd_open = f->is_image;
	r->sink_error = NULL;
	const char *error = read_package_member(r->pkg, &read, on_member_bytes, r);
	error = error && r->sink_error ? r->sink_error : error;
	if (!error && f->is_image)
		error = check_image(r, f);
	if (r->is_unzstd_open)
		close_unzstd(&r->z);
	r->is_unzstd_open = 0;
	return error;
}

static const char *receive_member(struct apply_run *r, const struct manifest_file *f)
{
	char tmp[PATH_MAX];
	size_t w = find_writer(f->role);
	WRITERS[w].build_tmp(r, tmp, sizeof tmp);
	enter_step(r, WRITERS[w].step);
	r->fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (r->fd < 0)
		return "io";
	const char *error = stream_member(r, f);
	if (!error && fsync(r->fd))
		error = find_io_error();
	if (close(r->fd) && !error)
		error = "io";
	return error;
}

/* ---- update --------------------------------------------------------------------------- */

static const char *check_update(struct apply_run *r)
{
	return strcmp(r->m->flavor, r->job->current_flavor) ? "wrong_flavor" : NULL;
}

/* A staged update the person replaces with this one goes too. */
static const char *clear_update(struct apply_run *r) { return remove_staged_update(r->folder) ? "io" : NULL; }

/* An extracted install copies the image's files out of it, next to the image. */
static unsigned long long measure_update_extra(const struct apply_run *r)
{
	return r->state.is_image ? 0 : find_manifest_file(r->m, "system")->image_size;
}

static int stage_system_image(struct apply_run *r)
{
	return rename(in_folder(r, SYSTEM_NEW_IMAGE ".tmp"), in_folder(r, SYSTEM_NEW_IMAGE));
}

static int stage_system_folder(struct apply_run *r)
{
	const struct apply_job *job = r->job;
	char image[PATH_MAX], tmp[PATH_MAX];
	snprintf(image, sizeof image, "%s", in_folder(r, SYSTEM_NEW_IMAGE ".tmp"));
	snprintf(tmp, sizeof tmp, "%s", in_folder(r, SYSTEM_NEW_DIR ".tmp"));
	make_dirs(r->image, 0700);
	if (job->ops->mount_image(job->env, image, r->image))
		return -1;
	remove_tree(tmp);
	int failed = copy_tree(r->image, tmp, on_copied, r) || rename(tmp, in_folder(r, SYSTEM_NEW_DIR));
	job->ops->umount(job->env, r->image);
	return failed ? -1 : remove_tree(image);
}

static int save_locale(struct apply_run *r)
{
	return *r->job->locale ? save_text_file(in_folder(r, UPDATE_LOCALE), r->job->locale) : 0;
}

/* The patches were made for the build this update replaces. */
static const char *commit_update(struct apply_run *r)
{
	int (*const stage)(struct apply_run *) = r->state.is_image ? stage_system_image : stage_system_folder;
	enter_step(r, "system");
	if (stage(r))
		return find_io_error();
	r->is_menu_changed = 1;
	return save_locale(r) || remove_patches(r->folder) || swap_in_file(r->folder, KERNEL_FILE, KERNEL_OLD) ||
			       swap_in_file(r->folder, INITRD_FILE, INITRD_OLD)
		       ? find_io_error()
		       : NULL;
}

/* ---- patch ---------------------------------------------------------------------------- */

/* Made for one build; with an update staged, the next boot runs another one. */
static const char *check_patch(struct apply_run *r)
{
	int is_staged = exists(in_folder(r, SYSTEM_NEW_IMAGE)) || exists(in_folder(r, SYSTEM_NEW_DIR));
	return strcmp(r->m->build, r->job->current_build) || is_staged ? "wrong_base" : NULL;
}

static const char *clear_patch(struct apply_run *r)
{
	remove_tmp_files(r);
	return make_dirs(in_folder(r, PATCHES_DIR), 0755) ? "io" : NULL;
}

static unsigned long long measure_no_extra(const struct apply_run *r)
{
	(void)r;
	return 0;
}

/* kernel.old keeps the kernel from before every patch: a second kernel patch does not push it out. */
static int swap_in_patch_file(struct apply_run *r, const char *role, const char *name, const char *old_name,
			      int is_taken)
{
	if (!find_manifest_file(r->m, role))
		return 0;
	r->is_menu_changed = 1;
	return swap_in_file(r->folder, name, is_taken ? NULL : old_name);
}

static const char *commit_patch(struct apply_run *r)
{
	const char *name = r->m->name;
	int has_overlay = find_manifest_file(r->m, "overlay") != NULL;
	enter_step(r, "system");
	if ((exists(in_patches(r, name, ".json")) || exists(in_patches(r, name, ".img"))) && drop_patch(r, name))
		return "io";
	struct replaced taken = find_replaced(r);
	if (make_dirs(in_folder(r, PATCHES_DIR), 0755) ||
	    save_patch_json(r, !!find_manifest_file(r->m, "kernel"), !!find_manifest_file(r->m, "initrd")))
		return find_io_error();
	if (has_overlay && rename(in_patches(r, name, ".img.tmp"), in_patches(r, name, ".img")))
		return "io";
	return swap_in_patch_file(r, "kernel", KERNEL_FILE, KERNEL_OLD, taken.kernel) ||
			       swap_in_patch_file(r, "initrd", INITRD_FILE, INITRD_OLD, taken.initrd)
		       ? "io"
		       : NULL;
}

/* ---- the run -------------------------------------------------------------------------- */

static const struct apply_type {
	const char *type;
	const char *(*check)(struct apply_run *);
	const char *(*clear)(struct apply_run *);
	unsigned long long (*measure_extra)(const struct apply_run *);
	unsigned long long margin;
	const char *(*commit)(struct apply_run *);
} TYPES[] = {
	{ "update", check_update, clear_update, measure_update_extra, PLAN_MARGIN, commit_update },
	{ "patch", check_patch, clear_patch, measure_no_extra, PATCH_MARGIN, commit_patch },
};

static const struct apply_type *find_type(const struct manifest *m)
{
	return &TYPES[strcmp(m->type, TYPES[0].type) != 0];
}

static unsigned long long measure_free(const char *dir)
{
	struct statvfs st;
	return statvfs(dir, &st) ? 0 : (unsigned long long)st.f_bavail * st.f_frsize;
}

static const char *step_open(struct apply_run *r)
{
	const struct apply_type *type = find_type(r->m);
	const char *error = open_folder(r);
	error = error ? error : type->check(r);
	error = error ? error : type->clear(r);
	if (error)
		return error;
	unsigned long long need = measure_package(r->m) + type->measure_extra(r) + type->margin;
	return measure_free(r->folder) < need ? "no_room" : NULL;
}

static const char *step_receive(struct apply_run *r)
{
	const char *error = NULL;
	for (int i = 0; !error && i < r->m->count; i++)
		error = receive_member(r, &r->m->files[i]);
	return error ? error : finish_package(r->pkg);
}

static const char *step_commit(struct apply_run *r) { return find_type(r->m)->commit(r); }

static const char *step_boot(struct apply_run *r)
{
	enter_step(r, "boot");
	const char *error = save_entry(r);
	add_done(r, BOOT_WEIGHT);
	return error;
}

static const char *step_finish(struct apply_run *r)
{
	enter_step(r, "finish");
	sync();
	r->done = r->total;
	report(r);
	return NULL;
}

static unsigned long long measure_stream(const struct manifest *m)
{
	unsigned long long total = 0;
	for (int i = 0; i < m->count; i++)
		total += m->files[i].size;
	return total;
}

static struct apply_run *start_run(const struct apply_job *job, install_progress progress, void *ctx)
{
	struct apply_run *r = calloc(1, sizeof *r);
	if (!r)
		return NULL;
	r->job = job;
	r->progress = progress;
	r->ctx = ctx;
	r->percent = -1;
	r->total = 1;
	snprintf(r->target, sizeof r->target, "%s/target", job->env->work);
	snprintf(r->image, sizeof r->image, "%s/image", job->env->work);
	snprintf(r->folder, sizeof r->folder, "%s/%s", r->target, job->folder);
	return r;
}

static void end_run(struct apply_run *r)
{
	close_folder(r);
	free(r);
}

static void on_no_progress(void *ctx, const char *step, int percent)
{
	(void)ctx;
	(void)step;
	(void)percent;
}

int run_apply(const struct apply_job *job, struct package *pkg, install_progress progress, void *ctx,
	      const char **error)
{
	static const char *(*const STEPS[])(struct apply_run *) = { step_open, step_receive, step_commit, step_boot,
								     step_finish };
	struct apply_run *r = start_run(job, progress, ctx);
	*error = r ? NULL : "io";
	if (!r)
		return -1;
	r->pkg = pkg;
	r->m = &pkg->manifest;
	r->step = "prepare";
	r->total = measure_stream(r->m) + BOOT_WEIGHT;
	report(r);
	for (size_t i = 0; i < sizeof STEPS / sizeof *STEPS && !*error; i++) {
		r->total += i == 1 ? find_type(r->m)->measure_extra(r) : 0;
		*error = STEPS[i](r);
	}
	if (*error && r->target_mounted)
		remove_tmp_files(r);
	end_run(r);
	return *error ? -1 : 0;
}

int run_remove_patch(const struct apply_job *job, const char *name, const char **error)
{
	struct apply_run *r = start_run(job, on_no_progress, NULL);
	*error = r ? NULL : "io";
	if (!r)
		return -1;
	*error = open_folder(r);
	if (!*error && !exists(in_patches(r, name, ".json")) && !exists(in_patches(r, name, ".img")))
		*error = "bad_request";
	if (!*error && drop_patch(r, name))
		*error = "io";
	*error = *error ? *error : save_entry(r);
	sync();
	end_run(r);
	return *error ? -1 : 0;
}

int list_patches(const struct apply_job *job, patch_listener each, void *ctx)
{
	struct apply_run *r = start_run(job, on_no_progress, NULL);
	const char *error = r ? open_folder(r) : "io";
	if (!error)
		each_patch(r, each, ctx);
	if (r)
		end_run(r);
	return error ? -1 : 0;
}
