/* The partitions pc-installd offers: file system, label, size, free space, ESP and live source. */
#pragma once
#include "model.h"
#include "parttable.h"
#include "sysops.h"

int find_partitions(const struct sys_env *env, const struct sys_ops *ops, struct partition *out, int max);
const struct partition *find_partition(const struct partition *parts, int count, const char *name);
/* The ESP on the target's disk, else the first ESP. */
const struct partition *find_esp(const struct partition *parts, int count, const struct partition *target);
/* source: the live source's partition, or its whole disk; every partition on it is marked. */
void apply_source_mark(struct partition *parts, int count, const char *source);
unsigned read_block_size(const struct sys_env *env, const char *disk);
int read_part_table(const struct sys_env *env, const char *disk, struct part_table *t);
