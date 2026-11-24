/* Checks an install request against the partition, the live source and what is already there. */
#pragma once
#include "model.h"

#define PLAN_MARGIN (256ULL << 20)
#define PLAN_MIN_DATA (2ULL << 30)

struct fs_rule {
	const char *type;
	const char *reason;     /* NULL when usable, else the error code */
	int allows_folders;     /* extracted system/ and userdata/ need Linux permissions and xattrs */
	unsigned long long max_file;
	const char *mount_type; /* kernel file system name */
};

struct plan_request {
	char part[32];
	char folder[128];
	const char *system_form; /* image | folder */
	const char *data_form;   /* image | folder | keep */
	unsigned long long data_size;
};

struct plan_result {
	const char *error; /* error code, see the repository README, "Errors" */
	const char *mode;  /* install | update */
	unsigned long long need;
};

const struct fs_rule *find_fs_rule(const char *type);
/* The kernel's name for a probed file system (ntfs is mounted with ntfs3). */
const char *find_mount_type(const char *fs);
int is_valid_folder(const char *folder);
/* 0 when the plan can run; else -1 with out->error set. */
int validate_plan(const struct plan_request *r, const struct partition *p, const struct source_sizes *src,
		  const struct install_state *state, const struct machine *m, struct plan_result *out);
