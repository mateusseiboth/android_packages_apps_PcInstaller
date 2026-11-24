/*
 * The install: copies the live source into <partition>/<folder>, creates the user data, and makes
 * the computer boot it through GRUB on the ESP. An update keeps the previous system as system.old
 * and puts it back if the new one cannot be written.
 */
#pragma once
#include "model.h"
#include "plan.h"
#include "source.h"
#include "sysops.h"

struct install_job {
	const struct sys_env *env;
	const struct sys_ops *ops;
	const struct live_source *src; /* mounted */
	const struct partition *target;
	const struct partition *esp;
	struct plan_request req;       /* already validated */
	char id[17];                   /* hex install id */
};

/* step: prepare, kernel, system, data, boot, finish; percent of the whole install. */
typedef void (*install_progress)(void *ctx, const char *step, int percent);

int run_install(const struct install_job *job, install_progress progress, void *ctx, const char **error);
/* What <dir> (a mounted install folder) already holds. */
void read_install_state(const char *dir, struct install_state *out);
