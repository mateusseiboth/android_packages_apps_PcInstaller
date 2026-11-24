#define _GNU_SOURCE
#include "install.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "biosboot.h"
#include "bootmenu.h"
#include "disks.h"
#include "efiboot.h"
#include "grubcfg.h"
#include "treecopy.h"
#include "util.h"

#define DATA_WEIGHT (64ULL << 20)
#define BOOT_WEIGHT (16ULL << 20)
#define EFI_DIR "EFI/LineageOS"
#define BIOS_DIR "pc-grub"
#define BOOT_DESCRIPTION "LineageOS"
#define EFI_LOADER "\\EFI\\LineageOS\\grubx64.efi"
#define FALLBACK_LOADER "EFI/BOOT/BOOTX64.EFI"

struct run {
	const struct install_job *job;
	install_progress progress;
	void *ctx;
	char target[PATH_MAX / 4], folder[PATH_MAX / 2], esp[PATH_MAX / 4], image[PATH_MAX / 4];
	int target_mounted, esp_mounted, image_mounted, rotated;
	char removed_id[64]; /* uninstall: the id read from the folder */
	struct machine_scan scan;
	unsigned long long total, done_before, step_weight, step_total, step_done;
	const char *step;
	int percent;
};

/* ---- progress ------------------------------------------------------------------------- */

static void report(struct run *r)
{
	unsigned long long step_part = r->step_total ? r->step_weight * r->step_done / r->step_total : 0;
	int percent = (int)((r->done_before + step_part) * 100 / r->total);
	if (percent == r->percent)
		return;
	r->percent = percent;
	r->progress(r->ctx, r->step, percent);
}

static void on_bytes(void *ctx, unsigned long long bytes)
{
	struct run *r = ctx;
	r->step_done += bytes;
	r->step_done = r->step_done < r->step_total ? r->step_done : r->step_total;
	report(r);
}

/* ---- paths ---------------------------------------------------------------------------- */

static void build_in(char *out, size_t cap, const char *dir, const char *name) { snprintf(out, cap, "%s/%s", dir, name); }

static const char *in_folder(const struct run *r, const char *name)
{
	static char out[4][PATH_MAX];
	static int next;
	char *p = out[next++ % 4];
	build_in(p, PATH_MAX, r->folder, name);
	return p;
}

static const char *in_source(const struct run *r, const char *name)
{
	static char out[4][PATH_MAX];
	static int next;
	char *p = out[next++ % 4];
	build_in(p, PATH_MAX, r->job->src->dir, name);
	return p;
}

static void build_device(char *out, size_t cap, const struct run *r, const char *name)
{
	snprintf(out, cap, "%s/%s", r->job->env->dev_dir, name);
}

/* ---- install state -------------------------------------------------------------------- */

static int exists(const char *path)
{
	struct stat st;
	return !lstat(path, &st);
}

static unsigned long long measure_path(const char *path)
{
	struct stat st;
	return lstat(path, &st) ? 0 : S_ISDIR(st.st_mode) ? measure_tree(path) : (unsigned long long)st.st_size;
}

static int is_id_file(const char *name)
{
	size_t n = strlen(name);
	return !strncmp(name, "install-", 8) && n > 11 && !strcmp(name + n - 3, ".id");
}

void read_install_state(const char *dir, struct install_state *out)
{
	char a[PATH_MAX], b[PATH_MAX];
	build_in(a, sizeof a, dir, "system.img");
	build_in(b, sizeof b, dir, "system");
	out->has_system = exists(a) || exists(b);
	build_in(a, sizeof a, dir, "data.img");
	build_in(b, sizeof b, dir, "userdata");
	out->has_data = exists(a) || exists(b);
	build_in(a, sizeof a, dir, "system.old.img");
	build_in(b, sizeof b, dir, "system.old");
	out->old_system_bytes = measure_path(a) + measure_path(b);
	build_in(a, sizeof a, dir, "system.img");
	out->is_image = exists(a);
	out->id[0] = 0;
	DIR *d = opendir(dir);
	for (struct dirent *e; d && (e = readdir(d));)
		if (is_id_file(e->d_name) && strlen(e->d_name) - 11 < sizeof out->id)
			snprintf(out->id, sizeof out->id, "%.*s", (int)strlen(e->d_name) - 11, e->d_name + 8);
	if (d)
		closedir(d);
}

/* ---- steps ---------------------------------------------------------------------------- */

static int mount_at(struct run *r, const char *name, const char *dir, const char *fs, int *mounted)
{
	char dev[PATH_MAX];
	build_device(dev, sizeof dev, r, name);
	make_dirs(dir, 0700);
	*mounted = !r->job->ops->mount(r->job->env, dev, dir, find_mount_type(fs), 0);
	return *mounted ? 0 : -1;
}

static const char *step_prepare(struct run *r)
{
	if (mount_at(r, r->job->target->name, r->target, r->job->target->fs, &r->target_mounted))
		return "mount_failed";
	return make_dirs(r->folder, 0755) ? "io_error" : NULL;
}

/* Writes name.tmp then renames it, so a cut-off copy never looks like a finished one. */
static int copy_into_folder(struct run *r, const char *name)
{
	char tmp[PATH_MAX];
	snprintf(tmp, sizeof tmp, "%s.tmp", in_folder(r, name));
	if (copy_file(in_source(r, name), tmp, on_bytes, r)) {
		remove_tree(tmp);
		return -1;
	}
	return rename(tmp, in_folder(r, name));
}

static const char *step_kernel(struct run *r)
{
	return copy_into_folder(r, "kernel") || copy_into_folder(r, "initrd.img") ? "copy_failed" : NULL;
}

/* ---- system: rotate, write, roll back ------------------------------------------------- */

static const char *const SYSTEM_NAMES[][2] = { { "system.img", "system.old.img" }, { "system", "system.old" } };

static int rotate_system(struct run *r)
{
	for (size_t i = 0; i < 2; i++)
		if (remove_tree(in_folder(r, SYSTEM_NAMES[i][1])))
			return -1;
	for (size_t i = 0; i < 2; i++)
		if (exists(in_folder(r, SYSTEM_NAMES[i][0])) && rename(in_folder(r, SYSTEM_NAMES[i][0]), in_folder(r, SYSTEM_NAMES[i][1])))
			return -1;
	r->rotated = 1;
	return 0;
}

static void restore_system(struct run *r)
{
	for (size_t i = 0; i < 2; i++)
		remove_tree(in_folder(r, SYSTEM_NAMES[i][0]));
	for (size_t i = 0; r->rotated && i < 2; i++)
		if (exists(in_folder(r, SYSTEM_NAMES[i][1])))
			rename(in_folder(r, SYSTEM_NAMES[i][1]), in_folder(r, SYSTEM_NAMES[i][0]));
}

static int write_system_image(struct run *r) { return copy_into_folder(r, "system.img"); }

static int mount_system_image(struct run *r)
{
	char dev[64];
	make_dirs(r->image, 0700);
	if (r->job->ops->attach_loop(r->job->env, in_source(r, "system.img"), dev, sizeof dev))
		return -1;
	r->image_mounted = !r->job->ops->mount(r->job->env, dev, r->image, "ext4", 1);
	return r->image_mounted ? 0 : -1;
}

static int write_system_folder(struct run *r)
{
	char tmp[PATH_MAX];
	snprintf(tmp, sizeof tmp, "%s.tmp", in_folder(r, "system"));
	if (mount_system_image(r))
		return -1;
	r->step_total = measure_tree(r->image);
	remove_tree(tmp);
	int failed = copy_tree(r->image, tmp, on_bytes, r) || rename(tmp, in_folder(r, "system"));
	r->job->ops->umount(r->job->env, r->image);
	r->image_mounted = 0;
	if (failed)
		remove_tree(tmp);
	return failed ? -1 : 0;
}

static const struct {
	const char *form;
	int (*write)(struct run *);
} SYSTEM_WRITERS[] = { { "image", write_system_image }, { "folder", write_system_folder } };

static int write_system(struct run *r)
{
	for (size_t i = 0; i < sizeof SYSTEM_WRITERS / sizeof *SYSTEM_WRITERS; i++)
		if (!strcmp(SYSTEM_WRITERS[i].form, r->job->req.system_form))
			return SYSTEM_WRITERS[i].write(r);
	return -1;
}

static const char *step_system(struct run *r)
{
	if (rotate_system(r))
		return "io_error";
	if (!write_system(r))
		return NULL;
	restore_system(r);
	return "copy_failed";
}

/* ---- data ----------------------------------------------------------------------------- */

static int write_data_image(struct run *r)
{
	char tmp[PATH_MAX];
	snprintf(tmp, sizeof tmp, "%s.tmp", in_folder(r, "data.img"));
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	int failed = fd < 0 || ftruncate(fd, (off_t)r->job->req.data_size);
	if (fd >= 0)
		close(fd);
	failed = failed || r->job->ops->make_ext4(r->job->env, tmp) || rename(tmp, in_folder(r, "data.img"));
	if (failed)
		remove_tree(tmp);
	return failed ? -1 : 0;
}

/* Android's init gives /data its owner and label on boot. */
static int write_data_folder(struct run *r)
{
	const char *dir = in_folder(r, "userdata");
	return mkdir(dir, 0771) || chmod(dir, 0771) ? -1 : 0;
}

static int keep_data(struct run *r)
{
	(void)r;
	return 0;
}

static const struct {
	const char *form;
	int (*write)(struct run *);
} DATA_WRITERS[] = { { "image", write_data_image }, { "folder", write_data_folder }, { "keep", keep_data } };

static const char *step_data(struct run *r)
{
	for (size_t i = 0; i < sizeof DATA_WRITERS / sizeof *DATA_WRITERS; i++)
		if (!strcmp(DATA_WRITERS[i].form, r->job->req.data_form))
			return DATA_WRITERS[i].write(r) ? "data_failed" : NULL;
	return "bad_request";
}

/* ---- boot: id file, GRUB on the ESP, UEFI entry --------------------------------------- */


static int save_id_file(struct run *r)
{
	DIR *d = opendir(r->folder);
	for (struct dirent *e; d && (e = readdir(d));)
		if (is_id_file(e->d_name))
			unlink(in_folder(r, e->d_name));
	if (d)
		closedir(d);
	char name[PATH_MAX], path[PATH_MAX * 2];
	build_id_file(name, sizeof name, r->job->req.folder, r->job->id);
	snprintf(path, sizeof path, "%s/%s", r->target, name);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	int failed = fd < 0 || dprintf(fd, "%s\n", r->job->id) < 0;
	return (fd >= 0 && close(fd)) || failed ? -1 : 0;
}

static const char *find_place_name(const struct partition *p) { return *p->label ? p->label : p->name; }

static int has_previous_system(struct run *r)
{
	return exists(in_folder(r, "system.old.img")) || exists(in_folder(r, "system.old"));
}

/* Our entry goes into every GRUB menu on the computer. */
static int save_menu_entries(struct run *r)
{
	char entry[8192];
	struct entry_params p = { r->job->req.folder, r->job->id, find_place_name(r->job->target), has_previous_system(r) };
	return build_install_entry(entry, sizeof entry, &p) ||
	       apply_menu_entry(r->job->env, r->job->ops, &r->scan, r->job->id, entry);
}

/* ---- UEFI: GRUB in EFI/LineageOS on the ESP, and a boot entry ------------------------- */

static int save_efi_loader(struct run *r)
{
	char dir[PATH_MAX], path[PATH_MAX * 2];
	build_in(dir, sizeof dir, r->esp, EFI_DIR);
	snprintf(path, sizeof path, "%s/grubx64.efi", dir);
	return make_dirs(dir, 0755) || copy_file(in_source(r, "grubx64.efi"), path, NULL, NULL) ? -1 : 0;
}

/* Firmware that ignores boot entries starts EFI/BOOT/BOOTX64.EFI; only fill it when nobody has. */
static int save_fallback_loader(struct run *r)
{
	char path[PATH_MAX], dir[PATH_MAX];
	build_in(path, sizeof path, r->esp, FALLBACK_LOADER);
	build_in(dir, sizeof dir, r->esp, "EFI/BOOT");
	if (exists(path))
		return 0;
	return make_dirs(dir, 0755) || copy_file(in_source(r, "grubx64.efi"), path, NULL, NULL) ? -1 : 0;
}

static int save_boot_entry(struct run *r)
{
	struct part_table t;
	struct efi_disk_location loc;
	uint8_t option[1024];
	uint16_t number;
	if (r->job->ops->mount_efivars(r->job->env))
		return -1;
	if (read_part_table(r->job->env, r->job->esp->disk, &t) || find_efi_location(&t, r->job->esp->number, &loc))
		return -1;
	size_t n = build_load_option(option, sizeof option, BOOT_DESCRIPTION, &loc, EFI_LOADER);
	return !n || apply_boot_entry(r->job->env->efivars, option, n, &number) ? -1 : 0;
}

static const char *setup_efi_boot(struct run *r)
{
	const struct partition *esp = r->job->esp;
	if (mount_at(r, esp->name, r->esp, esp->fs, &r->esp_mounted) || save_efi_loader(r) || save_fallback_loader(r))
		return "esp_failed";
	if (save_boot_entry(r))
		return "efi_failed";
	add_menu_place(&r->scan, esp, 1);
	return NULL;
}

/* ---- BIOS: core.img before the first partition, boot.img in sector 0 ------------------ */

static int find_core_start(struct run *r, uint64_t *start)
{
	const struct partition *target = r->job->target;
	struct part_table t;
	uint64_t sectors = (r->job->src->sizes.core + 511) / 512;
	return read_part_table(r->job->env, target->disk, &t) ||
	       find_core_place(&t, read_block_size(r->job->env, target->disk), sectors, start);
}

static int save_bios_loader(struct run *r, uint64_t start)
{
	const struct partition *target = r->job->target;
	char dir[PATH_MAX], modules[PATH_MAX * 2], disk[PATH_MAX], backup[PATH_MAX * 2];
	build_in(dir, sizeof dir, r->target, BIOS_DIR);
	snprintf(modules, sizeof modules, "%s/i386-pc", dir);
	snprintf(backup, sizeof backup, "%s/mbr-%s.bak", dir, target->disk);
	build_device(disk, sizeof disk, r, target->disk);
	if (make_dirs(modules, 0755) || copy_flat_dir(in_source(r, "grub-pc/i386-pc"), modules))
		return -1;
	return write_bios_boot(disk, in_source(r, "grub-pc/boot.img"), in_source(r, "grub-pc/core.img"), start, backup);
}

static const char *setup_bios_boot(struct run *r)
{
	uint64_t start = 0;
	if (find_core_start(r, &start))
		return "no_boot_room";
	if (save_bios_loader(r, start))
		return "boot_failed";
	add_menu_place(&r->scan, r->job->target, 0);
	return NULL;
}

static const char *step_boot(struct run *r)
{
	static const char *(*const SETUPS[])(struct run *) = { setup_bios_boot, setup_efi_boot };
	if (save_id_file(r))
		return "io_error";
	scan_machine(r->job->env, r->job->ops, r->job->parts, r->job->count, &r->scan);
	const char *error = SETUPS[r->job->is_uefi != 0](r);
	if (error)
		return error;
	return save_menu_entries(r) ? "menu_failed" : NULL;
}

static const char *step_finish(struct run *r)
{
	sync();
	(void)r;
	return NULL;
}

/* ---- the run -------------------------------------------------------------------------- */

static unsigned long long weight_none(const struct run *r) { (void)r; return 0; }
static unsigned long long weight_kernel(const struct run *r) { return r->job->src->sizes.kernel + r->job->src->sizes.initrd; }
static unsigned long long weight_system(const struct run *r) { return r->job->src->sizes.system; }
static unsigned long long weight_data(const struct run *r) { (void)r; return DATA_WEIGHT; }
static unsigned long long weight_boot(const struct run *r) { (void)r; return BOOT_WEIGHT; }

struct step {
	const char *name;
	const char *(*run)(struct run *);
	unsigned long long (*weight)(const struct run *);
};

static const struct step INSTALL_STEPS[] = {
	{ "prepare", step_prepare, weight_none },  { "kernel", step_kernel, weight_kernel },
	{ "system", step_system, weight_system }, { "data", step_data, weight_data },
	{ "boot", step_boot, weight_boot },	   { "finish", step_finish, weight_none },
};

static void release_mounts(struct run *r)
{
	const struct sys_ops *ops = r->job->ops;
	if (r->image_mounted)
		ops->umount(r->job->env, r->image);
	if (r->esp_mounted)
		ops->umount(r->job->env, r->esp);
	if (r->target_mounted)
		ops->umount(r->job->env, r->target);
	r->image_mounted = r->esp_mounted = r->target_mounted = 0;
}

static void init_run(struct run *r, const struct install_job *job, install_progress progress, void *ctx)
{
	memset(r, 0, sizeof *r);
	r->job = job;
	r->progress = progress;
	r->ctx = ctx;
	r->percent = -1;
	build_in(r->target, sizeof r->target, job->env->work, "target");
	build_in(r->esp, sizeof r->esp, job->env->work, "esp");
	build_in(r->image, sizeof r->image, job->env->work, "image");
	build_in(r->folder, sizeof r->folder, r->target, job->req.folder);
}

static const char *run_step(struct run *r, const struct step *step)
{
	r->step = step->name;
	r->step_weight = step->weight(r);
	r->step_total = r->step_weight;
	r->step_done = 0;
	report(r);
	const char *error = step->run(r);
	r->done_before += r->step_weight;
	return error;
}

static int run_steps(struct run *r, const struct step *steps, size_t count, const char **error)
{
	for (size_t i = 0; i < count; i++)
		r->total += steps[i].weight(r);
	r->total = r->total ? r->total : 1;
	*error = NULL;
	for (size_t i = 0; i < count && !*error; i++)
		*error = run_step(r, &steps[i]);
	release_mounts(r);
	if (!*error)
		report(r);
	return *error ? -1 : 0;
}

int run_install(const struct install_job *job, install_progress progress, void *ctx, const char **error)
{
	struct run *r = malloc(sizeof *r);
	if (!r) {
		*error = "io_error";
		return -1;
	}
	init_run(r, job, progress, ctx);
	int failed = run_steps(r, INSTALL_STEPS, sizeof INSTALL_STEPS / sizeof *INSTALL_STEPS, error);
	free(r);
	return failed;
}

/* ---- uninstall ------------------------------------------------------------------------ */

/* What an install wrote into its folder; anything else there is the user's and stays. */
static const char *const INSTALL_FILES[] = {
	"kernel",     "kernel.tmp",	"initrd.img",	  "initrd.img.tmp", "system.img", "system.img.tmp", "system",
	"system.tmp", "system.old.img", "system.old",	  "data.img",	    "data.img.tmp", "userdata",
};

static void read_install_id(struct run *r)
{
	DIR *d = opendir(r->folder);
	for (struct dirent *e; d && (e = readdir(d));)
		if (is_id_file(e->d_name))
			snprintf(r->removed_id, sizeof r->removed_id, "%.*s", (int)strlen(e->d_name) - 11, e->d_name + 8);
	if (d)
		closedir(d);
}

static const char *step_open(struct run *r)
{
	struct install_state state;
	if (mount_at(r, r->job->target->name, r->target, r->job->target->fs, &r->target_mounted))
		return "mount_failed";
	read_install_id(r);
	read_install_state(r->folder, &state);
	return state.has_system || *r->removed_id ? NULL : "not_installed";
}

static const char *step_remove(struct run *r)
{
	char id_file[128];
	for (size_t i = 0; i < sizeof INSTALL_FILES / sizeof *INSTALL_FILES; i++)
		if (remove_tree(in_folder(r, INSTALL_FILES[i])))
			return "io_error";
	snprintf(id_file, sizeof id_file, "install-%s.id", r->removed_id);
	if (*r->removed_id && remove_tree(in_folder(r, id_file)))
		return "io_error";
	rmdir(r->folder);
	return NULL;
}

/* With no ESP menu left, the UEFI boot entry would start nothing. */
static const char *remove_efi_entry(struct run *r)
{
	scan_machine(r->job->env, r->job->ops, r->job->parts, r->job->count, &r->scan);
	if (!r->job->is_uefi || count_efi_menus(&r->scan))
		return NULL;
	int failed = r->job->ops->mount_efivars(r->job->env) ||
		     remove_boot_entries(r->job->env->efivars, BOOT_DESCRIPTION) < 0;
	return failed ? "efi_failed" : NULL;
}

static const char *step_unregister(struct run *r)
{
	if (!*r->removed_id)
		return NULL;
	scan_machine(r->job->env, r->job->ops, r->job->parts, r->job->count, &r->scan);
	if (drop_menu_entry(r->job->env, r->job->ops, &r->scan, r->removed_id))
		return "menu_failed";
	return remove_efi_entry(r);
}

static unsigned long long weight_one(const struct run *r) { (void)r; return 1; }
static unsigned long long weight_remove(const struct run *r) { (void)r; return 6; }
static unsigned long long weight_unregister(const struct run *r) { (void)r; return 2; }

static const struct step UNINSTALL_STEPS[] = {
	{ "prepare", step_open, weight_one },	{ "remove", step_remove, weight_remove },
	{ "boot", step_unregister, weight_unregister }, { "finish", step_finish, weight_one },
};

int run_uninstall(const struct install_job *job, install_progress progress, void *ctx, const char **error)
{
	struct run *r = malloc(sizeof *r);
	if (!r) {
		*error = "io_error";
		return -1;
	}
	init_run(r, job, progress, ctx);
	int failed = run_steps(r, UNINSTALL_STEPS, sizeof UNINSTALL_STEPS / sizeof *UNINSTALL_STEPS, error);
	free(r);
	return failed;
}
