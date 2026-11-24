/* pc-installd's requests: one line in, zero or more data lines out, then OK or ERR code=... */
#pragma once
#include "model.h"
#include "source.h"
#include "sysops.h"

#define PROTOCOL_VERSION 1
#define MAX_PARTITIONS 64

struct server {
	const struct sys_env *env;
	const struct sys_ops *ops;
	struct live_source src;
	int src_state; /* 0 not looked yet, 1 mounted, -1 missing */
	struct partition parts[MAX_PARTITIONS];
	int count;
};

typedef void (*reply_fn)(void *ctx, const char *line);

void server_init(struct server *s, const struct sys_env *env, const struct sys_ops *ops);
void handle_line(struct server *s, const char *line, reply_fn reply, void *ctx);
