#include "proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_safe(unsigned char c) { return isalnum(c) || strchr("._/:+,-", c); }

static int hex_value(char c)
{
	static const char DIGITS[] = "0123456789abcdef";
	const char *p = c ? strchr(DIGITS, tolower((unsigned char)c)) : NULL;
	return p ? (int)(p - DIGITS) : -1;
}

static int decode(const char *in, size_t len, char *out, size_t cap)
{
	size_t n = 0;
	for (size_t i = 0; i < len; i++, n++) {
		if (n + 1 >= cap)
			return -1;
		if (in[i] != '%') {
			out[n] = in[i];
			continue;
		}
		int hi = i + 2 < len ? hex_value(in[i + 1]) : -1;
		int lo = hi >= 0 ? hex_value(in[i + 2]) : -1;
		if (lo < 0)
			return -1;
		out[n] = (char)(hi << 4 | lo);
		i += 2;
	}
	out[n] = 0;
	return 0;
}

static int is_word(const char *w)
{
	for (; *w; w++)
		if (!isupper((unsigned char)*w) && *w != '_')
			return 0;
	return 1;
}

static int parse_field(const char *tok, struct msg_field *f)
{
	const char *eq = strchr(tok, '=');
	size_t key_len = eq ? (size_t)(eq - tok) : 0;
	if (!key_len || key_len >= sizeof f->key)
		return -1;
	memcpy(f->key, tok, key_len);
	f->key[key_len] = 0;
	return decode(eq + 1, strlen(eq + 1), f->value, sizeof f->value);
}

int parse_msg(const char *line, struct msg *m)
{
	char buf[LINE_MAX_LEN];
	snprintf(buf, sizeof buf, "%s", line);
	buf[strcspn(buf, "\r\n")] = 0;
	*m = (struct msg){ 0 };
	char *save = NULL;
	const char *word = strtok_r(buf, " ", &save);
	if (!word || strlen(word) >= sizeof m->word || !is_word(word))
		return -1;
	strcpy(m->word, word);
	for (char *tok = strtok_r(NULL, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
		if (m->count == MSG_MAX_FIELDS || parse_field(tok, &m->fields[m->count]))
			return -1;
		m->count++;
	}
	return 0;
}

const char *find_field(const struct msg *m, const char *key)
{
	for (int i = 0; i < m->count; i++)
		if (!strcmp(m->fields[i].key, key))
			return m->fields[i].value;
	return NULL;
}

int find_u64(const struct msg *m, const char *key, unsigned long long *out)
{
	const char *v = find_field(m, key);
	char *end = NULL;
	if (!v || !isdigit((unsigned char)*v))
		return -1;
	unsigned long long n = strtoull(v, &end, 10);
	if (*end)
		return -1;
	*out = n;
	return 0;
}

static void append(struct line *l, const char *fmt, const char *s)
{
	int n = snprintf(l->text + l->len, sizeof l->text - l->len, fmt, s);
	l->len = n < 0 ? l->len : l->len + (size_t)n < sizeof l->text ? l->len + (size_t)n : sizeof l->text - 1;
}

void line_begin(struct line *l, const char *word)
{
	l->len = 0;
	l->text[0] = 0;
	append(l, "%s", word);
}

void line_add(struct line *l, const char *key, const char *value)
{
	append(l, " %s=", key);
	for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
		char enc[4];
		snprintf(enc, sizeof enc, is_safe(*p) ? "%c" : "%%%02X", *p);
		append(l, "%s", enc);
	}
}

void line_add_u64(struct line *l, const char *key, unsigned long long value)
{
	char num[24];
	snprintf(num, sizeof num, "%llu", value);
	line_add(l, key, num);
}

const char *line_end(struct line *l)
{
	append(l, "%s", "\n");
	return l->text;
}
