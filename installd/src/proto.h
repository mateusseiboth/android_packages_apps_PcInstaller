/*
 * The app <-> pc-installd line protocol. A line is an upper-case WORD followed by key=value fields
 * separated by single spaces. Values are percent-encoded: any byte outside [A-Za-z0-9._/:+,-] is
 * written as %XX, so a value never holds a space, '=' or a newline. See the repository README, "Protocol".
 */
#pragma once
#include <stddef.h>

#define MSG_MAX_FIELDS 24
#define MSG_KEY_MAX 32
#define MSG_VALUE_MAX 512
#define LINE_MAX_LEN 4096

struct msg_field {
	char key[MSG_KEY_MAX];
	char value[MSG_VALUE_MAX];
};

struct msg {
	char word[MSG_KEY_MAX];
	int count;
	struct msg_field fields[MSG_MAX_FIELDS];
};

int parse_msg(const char *line, struct msg *m);
const char *find_field(const struct msg *m, const char *key);
int find_u64(const struct msg *m, const char *key, unsigned long long *out);

struct line {
	char text[LINE_MAX_LEN];
	size_t len;
};

void line_begin(struct line *l, const char *word);
void line_add(struct line *l, const char *key, const char *value);
void line_add_u64(struct line *l, const char *key, unsigned long long value);
const char *line_end(struct line *l);
