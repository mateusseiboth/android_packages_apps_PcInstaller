#include "server.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include "biosboot.h"
#include "disks.h"
#include "efiboot.h"
#include "install.h"
#include "mok.h"
#include "plan.h"
#include "proto.h"
#include "util.h"

struct request {
	struct server *s;
	const struct msg *m;
	reply_fn reply;
	void *ctx;
};

void server_init(struct server *s, const struct sys_env *env, const struct sys_ops *ops)
{
	*s = (struct server){ .env = env, .ops = ops };
}

/* ---- replies -------------------------------------------------------------------------- */

static void send_line(const struct request *q, struct line *l) { q->reply(q->ctx, line_end(l)); }

static void send_word(const struct request *q, const char *word)
{
	struct line l;
	line_begin(&l, word);
	send_line(q, &l);
}

static void send_error(const struct request *q, const char *code)
{
	struct line l;
	line_begin(&l, "ERR");
	line_add(&l, "code", code);
	send_line(q, &l);
}

/* ---- shared state --------------------------------------------------------------------- */

static int ensure_source(struct server *s)
{
	if (!s->src_state)
		s->src_state = !find_live_source(s->env, s->ops, &s->src) && !mount_live_source(s->env, s->ops, &s->src) ? 1 : -1;
	return s->src_state > 0 ? 0 : -1;
}

static int ensure_running(struct server *s)
{
	if (!s->running_state)
		s->running_state = find_running_install(s->env, s->ops, &s->running) ? -1 : 1;
	return s->running_state > 0 ? 0 : -1;
}

static void refresh_partitions(struct server *s)
{
	s->count = find_partitions(s->env, s->ops, s->parts, MAX_PARTITIONS);
	if (!ensure_source(s))
		apply_source_mark(s->parts, s->count, s->src.disk);
}

static const struct partition *find_requested(const struct request *q)
{
	const char *name = find_field(q->m, "part");
	if (!q->s->count)
		refresh_partitions(q->s);
	return name ? find_partition(q->s->parts, q->s->count, name) : NULL;
}

static int has_boot_room(const struct server *s, const struct partition *target)
{
	struct part_table t;
	uint64_t start = 0, sectors = (s->src.sizes.core + 511) / 512;
	return !read_part_table(s->env, target->disk, &t) &&
	       !find_core_place(&t, read_block_size(s->env, target->disk), sectors, &start);
}

static struct machine read_machine(const struct server *s, const struct partition *target)
{
	return (struct machine){ .is_uefi = is_dir(s->env->efivars),
				 .has_esp = find_esp(s->parts, s->count, target) != NULL,
				 .has_boot_room = has_boot_room(s, target) };
}

/* ---- looking into an install folder --------------------------------------------------- */

static int inspect_target(struct server *s, const struct partition *p, const char *folder, struct install_state *out)
{
	char dev[512], dir[512], path[1024];
	*out = (struct install_state){ 0 };
	snprintf(dev, sizeof dev, "%s/%s", s->env->dev_dir, p->name);
	snprintf(dir, sizeof dir, "%s/target", s->env->work);
	make_dirs(dir, 0700);
	if (s->ops->mount(s->env, dev, dir, find_mount_type(p->fs), 1))
		return -1;
	snprintf(path, sizeof path, "%s/%s", dir, folder);
	read_install_state(path, out);
	s->ops->umount(s->env, dir);
	return 0;
}

static const char *find_target(const struct request *q, const struct partition **p, const char **folder)
{
	*p = find_requested(q);
	*folder = find_field(q->m, "folder");
	if (!*p)
		return "unknown_partition";
	return *folder && is_valid_folder(*folder) ? NULL : "bad_folder";
}

static int is_running_install(struct server *s, const char *part, const char *folder)
{
	return !ensure_running(s) && !strcmp(s->running.part, part) && !strcmp(s->running.folder, folder);
}

/* ---- the builder's key and shim ------------------------------------------------------ */

#define KEY_FILE "ENROLL_THIS_KEY_IN_MOKMANAGER.der"
#define MAX_KEY_BYTES (64 * 1024)

struct mok_state {
	int enrolled, queued;
};

static int load_source_key(struct server *s, unsigned char **der, size_t *len)
{
	char path[PATH_MAX + 64];
	if (ensure_source(s))
		return -1;
	snprintf(path, sizeof path, "%s/" KEY_FILE, s->src.dir);
	return load_file(path, MAX_KEY_BYTES, der, len);
}

static int open_efivars(const struct server *s)
{
	return is_dir(s->env->efivars) && !s->ops->mount_efivars(s->env) ? 0 : -1;
}

/* Unknown (no source, no UEFI) reads as not enrolled. */
static struct mok_state read_mok_state(struct server *s)
{
	struct mok_state st = { 0, 0 };
	unsigned char *der = NULL;
	size_t len = 0;
	if (open_efivars(s) || load_source_key(s, &der, &len))
		return st;
	st.enrolled = is_mok_enrolled(s->env->efivars, der, len);
	st.queued = is_mok_queued(s->env->efivars, der, len);
	free(der);
	return st;
}

static void add_mok_fields(struct server *s, struct line *l)
{
	struct mok_state st = read_mok_state(s);
	line_add_u64(l, "mok_enrolled", (unsigned long long)st.enrolled);
	line_add_u64(l, "mok_queued", (unsigned long long)st.queued);
}

/* ---- INFO ----------------------------------------------------------------------------- */

static int read_secure_boot(const struct server *s)
{
	return !open_efivars(s) && is_secure_boot_on(s->env->efivars);
}

static const char *find_running_form(struct server *s)
{
	struct install_state st;
	const struct partition *p;
	if (!s->count)
		refresh_partitions(s);
	p = find_partition(s->parts, s->count, s->running.part);
	if (!p || inspect_target(s, p, s->running.folder, &st))
		return "";
	return st.is_image ? "image" : "folder";
}

static void add_running_fields(struct server *s, struct line *l)
{
	int installed = !ensure_running(s);
	line_add_u64(l, "installed", (unsigned long long)installed);
	line_add(l, "install_part", installed ? s->running.part : "");
	line_add(l, "install_folder", installed ? s->running.folder : "");
	line_add(l, "install_system", installed ? find_running_form(s) : "");
	line_add(l, "install_version", installed ? s->running.build.version : "");
	line_add_u64(l, "install_build_utc", installed ? s->running.build.utc : 0);
}

static const char *handle_info(const struct request *q)
{
	struct server *s = q->s;
	struct line l;
	int live = !ensure_source(s);
	line_begin(&l, "INFO");
	line_add_u64(&l, "version", PROTOCOL_VERSION);
	line_add_u64(&l, "uefi", (unsigned long long)is_dir(s->env->efivars));
	line_add_u64(&l, "secure_boot", (unsigned long long)read_secure_boot(s));
	add_mok_fields(s, &l);
	line_add_u64(&l, "live", (unsigned long long)live);
	line_add(&l, "source", live ? s->src.name : "");
	line_add(&l, "folder", live ? s->src.folder : "");
	line_add_u64(&l, "kernel", live ? s->src.sizes.kernel : 0);
	line_add_u64(&l, "initrd", live ? s->src.sizes.initrd : 0);
	line_add_u64(&l, "system", live ? s->src.sizes.system : 0);
	add_running_fields(s, &l);
	send_line(q, &l);
	return NULL;
}

/* ---- LIST ----------------------------------------------------------------------------- */

static const char *find_reason(const struct partition *p)
{
	if (p->is_source)
		return "source_partition";
	const char *reason = find_fs_rule(p->fs)->reason;
	return reason ? reason : p->cannot_mount ? "mount_failed" : NULL;
}

static void send_partition(const struct request *q, const struct partition *p)
{
	const struct fs_rule *rule = find_fs_rule(p->fs);
	const char *reason = find_reason(p);
	struct line l;
	line_begin(&l, "PART");
	line_add(&l, "name", p->name);
	line_add(&l, "disk", p->disk);
	line_add(&l, "fs", p->fs);
	line_add(&l, "label", p->label);
	line_add_u64(&l, "size", p->size);
	line_add_u64(&l, "free", p->free);
	line_add_u64(&l, "esp", (unsigned long long)p->is_esp);
	line_add_u64(&l, "source", (unsigned long long)p->is_source);
	line_add_u64(&l, "usable", !reason);
	line_add(&l, "reason", reason ? reason : "");
	line_add_u64(&l, "folders", (unsigned long long)rule->allows_folders);
	line_add_u64(&l, "max_file", rule->max_file);
	send_line(q, &l);
}

static const char *handle_list(const struct request *q)
{
	refresh_partitions(q->s);
	for (int i = 0; i < q->s->count; i++)
		send_partition(q, &q->s->parts[i]);
	return NULL;
}

/* ---- INSPECT -------------------------------------------------------------------------- */

static const char *handle_inspect(const struct request *q)
{
	const struct partition *p;
	const char *folder;
	struct install_state st;
	const char *error = find_target(q, &p, &folder);
	if (error)
		return error;
	if (find_fs_rule(p->fs)->reason || inspect_target(q->s, p, folder, &st))
		return "mount_failed";
	struct line l;
	line_begin(&l, "STATE");
	line_add_u64(&l, "system", (unsigned long long)st.has_system);
	line_add_u64(&l, "data", (unsigned long long)st.has_data);
	line_add_u64(&l, "old", st.old_system_bytes);
	send_line(q, &l);
	return NULL;
}

/* ---- CHECK and INSTALL ---------------------------------------------------------------- */

struct checked {
	const struct partition *target;
	struct plan_request req;
	struct install_state state;
	struct machine machine;
	struct plan_result plan;
};

static void read_request(const struct msg *m, struct plan_request *r)
{
	*r = (struct plan_request){ .system_form = find_field(m, "system"), .data_form = find_field(m, "data") };
	snprintf(r->part, sizeof r->part, "%s", find_field(m, "part") ? find_field(m, "part") : "");
	snprintf(r->folder, sizeof r->folder, "%s", find_field(m, "folder") ? find_field(m, "folder") : "");
	find_u64(m, "data_size", &r->data_size);
}

static const char *check_request(const struct request *q, struct checked *c)
{
	*c = (struct checked){ 0 };
	read_request(q->m, &c->req);
	if (ensure_source(q->s))
		return "source_missing";
	c->target = find_requested(q);
	if (!c->target)
		return "unknown_partition";
	int can_look = is_valid_folder(c->req.folder) && !find_fs_rule(c->target->fs)->reason && !c->target->is_source;
	if (can_look && inspect_target(q->s, c->target, c->req.folder, &c->state))
		return "mount_failed";
	c->machine = read_machine(q->s, c->target);
	return validate_plan(&c->req, c->target, &q->s->src.sizes, &c->state, &c->machine, &c->plan) ? c->plan.error
												    : NULL;
}

static const char *handle_check(const struct request *q)
{
	struct checked c;
	const char *error = check_request(q, &c);
	if (error)
		return error;
	const struct partition *esp = find_esp(q->s->parts, q->s->count, c.target);
	struct line l;
	line_begin(&l, "PLAN");
	line_add(&l, "mode", c.plan.mode);
	line_add_u64(&l, "need", c.plan.need);
	line_add_u64(&l, "free", c.target->free);
	line_add(&l, "boot", c.machine.is_uefi ? "uefi" : "bios");
	line_add(&l, "esp", c.machine.is_uefi && esp ? esp->name : "");
	send_line(q, &l);
	return NULL;
}

static void on_progress(void *ctx, const char *step, int percent)
{
	const struct request *q = ctx;
	struct line l;
	line_begin(&l, "PROGRESS");
	line_add(&l, "step", step);
	line_add_u64(&l, "percent", (unsigned long long)percent);
	send_line(q, &l);
}

/* An update keeps the install's id, so its menu entry is replaced rather than doubled. */
static void build_install_id(const struct install_state *st, char *out, size_t cap)
{
	unsigned char bytes[8];
	if (*st->id) {
		snprintf(out, cap, "%s", st->id);
		return;
	}
	if (getrandom(bytes, sizeof bytes, 0) != sizeof bytes) {
		unsigned long long t = (unsigned long long)time(NULL) ^ (unsigned long long)getpid() << 32;
		memcpy(bytes, &t, sizeof bytes);
	}
	for (size_t i = 0; i < sizeof bytes && 2 * i + 2 < cap; i++)
		snprintf(out + 2 * i, cap - 2 * i, "%02x", bytes[i]);
}

static struct install_job build_job(const struct request *q, const struct partition *target,
				    const struct plan_request *req)
{
	struct server *s = q->s;
	int is_uefi = is_dir(s->env->efivars);
	return (struct install_job){ .env = s->env, .ops = s->ops, .src = &s->src, .target = target,
				     .esp = is_uefi ? find_esp(s->parts, s->count, target) : NULL, .parts = s->parts,
				     .count = s->count, .is_uefi = is_uefi, .req = *req };
}

static const char *handle_install(const struct request *q)
{
	struct checked c;
	const char *error = check_request(q, &c);
	if (error)
		return error;
	struct install_job job = build_job(q, c.target, &c.req);
	build_install_id(&c.state, job.id, sizeof job.id);
	run_install(&job, on_progress, (void *)q, &error);
	q->s->count = 0; /* free space changed */
	return error;
}

/* ---- UNINSTALL ------------------------------------------------------------------------ */

static const char *handle_uninstall(const struct request *q)
{
	const struct partition *p;
	const char *folder;
	const char *error = find_target(q, &p, &folder);
	if (error)
		return error;
	if (is_running_install(q->s, p->name, folder))
		return "running_system";
	struct plan_request req = { .data_form = "keep", .system_form = "image" };
	snprintf(req.folder, sizeof req.folder, "%s", folder);
	snprintf(req.part, sizeof req.part, "%s", p->name);
	struct install_job job = build_job(q, p, &req);
	run_uninstall(&job, on_progress, (void *)q, &error);
	q->s->count = 0;
	return error;
}

/* ---- FIND_UPDATE: a live USB drive plugged into an installed system ------------------ */

static const char *handle_find_update(const struct request *q)
{
	struct server *s = q->s;
	struct build_info build;
	if (ensure_running(s))
		return "not_installed";
	s->count = find_partitions(s->env, s->ops, s->parts, MAX_PARTITIONS);
	if (find_usb_source(s->env, s->ops, s->parts, s->count, s->running.part, &s->src))
		return "no_update_source";
	s->src_state = 1;
	apply_source_mark(s->parts, s->count, s->src.disk);
	if (read_source_build(s->env, s->ops, &s->src, &build))
		return "source_missing";
	struct line l;
	line_begin(&l, "UPDATE");
	line_add(&l, "source", s->src.name);
	line_add(&l, "version", build.version);
	line_add_u64(&l, "build_utc", build.utc);
	line_add(&l, "current_version", s->running.build.version);
	line_add_u64(&l, "current_utc", s->running.build.utc);
	line_add_u64(&l, "newer", build.utc > s->running.build.utc);
	add_mok_fields(s, &l);
	send_line(q, &l);
	return NULL;
}

/* ---- ENROLL_KEY: MokManager asks to enroll the source's key on the next start --------- */

static const char *queue_key(struct server *s, const unsigned char *der, size_t len, char *password)
{
	uint64_t random = 0;
	if (open_efivars(s) || s->ops->fill_random(s->env, &random, sizeof random))
		return "efi_failed";
	build_mok_password(random, password);
	return queue_mok_enrollment(s->env->efivars, der, len, password) ? "efi_failed" : NULL;
}

static const char *handle_enroll_key(const struct request *q)
{
	unsigned char *der = NULL;
	size_t len = 0;
	char password[MOK_PASSWORD_LEN + 1];
	if (load_source_key(q->s, &der, &len))
		return "source_missing";
	const char *error = queue_key(q->s, der, len, password);
	free(der);
	if (error)
		return error;
	struct line l;
	line_begin(&l, "MOK");
	line_add(&l, "password", password);
	send_line(q, &l);
	return NULL;
}

/* ---- dispatch ------------------------------------------------------------------------- */

static const struct {
	const char *word;
	const char *(*handle)(const struct request *);
} HANDLERS[] = {
	{ "INFO", handle_info },	 { "LIST", handle_list },	    { "INSPECT", handle_inspect },
	{ "CHECK", handle_check },	 { "INSTALL", handle_install },	    { "UNINSTALL", handle_uninstall },
	{ "FIND_UPDATE", handle_find_update }, { "ENROLL_KEY", handle_enroll_key },
};

static const char *dispatch(const struct request *q)
{
	for (size_t i = 0; i < sizeof HANDLERS / sizeof *HANDLERS; i++)
		if (!strcmp(HANDLERS[i].word, q->m->word))
			return HANDLERS[i].handle(q);
	return "bad_request";
}

void handle_line(struct server *s, const char *line, reply_fn reply, void *ctx)
{
	struct msg m;
	struct request q = { s, &m, reply, ctx };
	const char *error = parse_msg(line, &m) ? "bad_request" : dispatch(&q);
	if (error) {
		send_error(&q, error);
		return;
	}
	send_word(&q, "OK");
}
