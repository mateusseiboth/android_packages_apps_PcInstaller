#include "json.h"

#include <string.h>

#define MAX_DEPTH 16

struct parser {
	const char *s;
	size_t len, pos;
	struct json_token *t;
	int max, count, depth;
};

static int parse_value(struct parser *p);

static int peek(const struct parser *p) { return p->pos < p->len ? (unsigned char)p->s[p->pos] : -1; }

static void skip_space(struct parser *p)
{
	while (p->pos < p->len && strchr(" \t\r\n", p->s[p->pos]) && p->s[p->pos])
		p->pos++;
}

static int expect(struct parser *p, char c)
{
	skip_space(p);
	if (peek(p) != (unsigned char)c)
		return -1;
	p->pos++;
	return 0;
}

static int add_token(struct parser *p, enum json_type type)
{
	if (p->count == p->max)
		return -1;
	p->t[p->count] = (struct json_token){ .type = type, .start = p->pos };
	return p->count++;
}

static int close_token(struct parser *p, int i)
{
	p->t[i].end = p->pos;
	p->t[i].next = p->count;
	return i;
}

/* ---- scalars -------------------------------------------------------------------------- */

static int is_hex(int c) { return c >= 0 && strchr("0123456789abcdefABCDEF", c) && c; }

static int skip_escape(struct parser *p)
{
	int c = peek(p);
	p->pos++;
	if (c == 'u') {
		for (int i = 0; i < 4; i++, p->pos++)
			if (!is_hex(peek(p)))
				return -1;
		return 0;
	}
	return c > 0 && strchr("\"\\/bfnrt", c) ? 0 : -1;
}

static int skip_char(struct parser *p)
{
	int c = peek(p);
	if (c < 0x20)
		return -1;
	p->pos++;
	return c == '\\' ? skip_escape(p) : 0;
}

static int parse_string(struct parser *p)
{
	p->pos++;
	int i = add_token(p, JSON_STRING);
	if (i < 0)
		return -1;
	while (peek(p) != '"')
		if (skip_char(p))
			return -1;
	close_token(p, i);
	p->pos++;
	return i;
}

static void skip_digits(struct parser *p, int *seen)
{
	*seen = 0;
	for (int c = peek(p); c >= '0' && c <= '9'; c = peek(p), *seen = 1)
		p->pos++;
}

/* -?digits(.digits)?([eE][+-]?digits)? */
static int parse_number(struct parser *p)
{
	int i = add_token(p, JSON_NUMBER), seen = 0;
	p->pos += peek(p) == '-';
	skip_digits(p, &seen);
	if (i < 0 || !seen)
		return -1;
	if (peek(p) == '.' && (p->pos++, skip_digits(p, &seen), !seen))
		return -1;
	if (peek(p) == 'e' || peek(p) == 'E') {
		p->pos++;
		p->pos += peek(p) == '+' || peek(p) == '-';
		if (skip_digits(p, &seen), !seen)
			return -1;
	}
	return close_token(p, i);
}

static int parse_literal(struct parser *p)
{
	static const char *const WORDS[] = { "true", "false", "null" };
	int i = add_token(p, JSON_LITERAL);
	for (size_t w = 0; i >= 0 && w < sizeof WORDS / sizeof *WORDS; w++) {
		size_t n = strlen(WORDS[w]);
		if (p->len - p->pos >= n && !memcmp(p->s + p->pos, WORDS[w], n)) {
			p->pos += n;
			return close_token(p, i);
		}
	}
	return -1;
}

/* ---- containers ----------------------------------------------------------------------- */

static int parse_member(struct parser *p)
{
	skip_space(p);
	if (peek(p) != '"' || parse_string(p) < 0 || expect(p, ':'))
		return -1;
	return parse_value(p) < 0 ? -1 : 0;
}

static int parse_item(struct parser *p) { return parse_value(p) < 0 ? -1 : 0; }

static int parse_container(struct parser *p, enum json_type type, char close, int (*parse_one)(struct parser *))
{
	int i = add_token(p, type);
	p->pos++;
	if (i < 0 || ++p->depth > MAX_DEPTH)
		return -1;
	skip_space(p);
	int done = peek(p) == close;
	while (!done) {
		if (parse_one(p))
			return -1;
		p->t[i].count++;
		skip_space(p);
		done = peek(p) == close;
		if (!done && expect(p, ','))
			return -1;
	}
	p->pos++;
	p->depth--;
	return close_token(p, i);
}

static int parse_object(struct parser *p) { return parse_container(p, JSON_OBJECT, '}', parse_member); }
static int parse_array(struct parser *p) { return parse_container(p, JSON_ARRAY, ']', parse_item); }

static const struct {
	const char *first;
	int (*parse)(struct parser *);
} VALUES[] = {
	{ "{", parse_object }, { "[", parse_array }, { "\"", parse_string }, { "-0123456789", parse_number },
	{ "tfn", parse_literal },
};

static int parse_value(struct parser *p)
{
	skip_space(p);
	int c = peek(p);
	for (size_t i = 0; c > 0 && i < sizeof VALUES / sizeof *VALUES; i++)
		if (strchr(VALUES[i].first, c))
			return VALUES[i].parse(p);
	return -1;
}

int parse_json(const char *text, size_t len, struct json_token *tokens, int max)
{
	struct parser p = { .s = text, .len = len, .t = tokens, .max = max };
	if (parse_value(&p) < 0)
		return -1;
	skip_space(&p);
	return p.pos == len ? p.count : -1;
}

/* ---- lookups -------------------------------------------------------------------------- */

static int is_string(const char *text, const struct json_token *t, const char *want)
{
	size_t n = strlen(want);
	return t->type == JSON_STRING && t->end - t->start == n && !memcmp(text + t->start, want, n);
}

int find_json_key(const char *text, const struct json_token *t, int obj, const char *key)
{
	if (obj < 0 || t[obj].type != JSON_OBJECT)
		return -1;
	for (int i = 0, k = obj + 1; i < t[obj].count; i++, k = t[k + 1].next)
		if (is_string(text, &t[k], key))
			return k + 1;
	return -1;
}

int find_json_item(const struct json_token *t, int arr, int i)
{
	if (arr < 0 || t[arr].type != JSON_ARRAY || i < 0 || i >= t[arr].count)
		return -1;
	int k = arr + 1;
	for (int n = 0; n < i; n++)
		k = t[k].next;
	return k;
}

/* ---- copying out ---------------------------------------------------------------------- */

static unsigned read_hex4(const char *s)
{
	unsigned v = 0;
	for (int i = 0; i < 4; i++) {
		char c = s[i];
		v = v << 4 | (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
	}
	return v;
}

/* \uXXXX, or a surrogate pair \uD8xx\uDCxx; *len gets the escape's length after the backslash. */
static long read_code_point(const char *s, size_t avail, size_t *len)
{
	unsigned hi = read_hex4(s + 1);
	*len = 5;
	if (hi < 0xd800 || hi > 0xdfff)
		return hi;
	if (hi > 0xdbff || avail < 11 || s[5] != '\\' || s[6] != 'u')
		return -1;
	unsigned lo = read_hex4(s + 7);
	*len = 11;
	return lo < 0xdc00 || lo > 0xdfff ? -1 : 0x10000 + ((long)(hi - 0xd800) << 10) + (lo - 0xdc00);
}

static size_t encode_utf8(unsigned long c, char *out)
{
	static const struct {
		unsigned long below;
		unsigned char lead;
		int extra;
	} FORMS[] = { { 0x80, 0x00, 0 }, { 0x800, 0xc0, 1 }, { 0x10000, 0xe0, 2 }, { 0x110000, 0xf0, 3 } };
	size_t f = 0;
	while (c >= FORMS[f].below)
		f++;
	out[0] = (char)(FORMS[f].lead | c >> (6 * FORMS[f].extra));
	for (int i = 1; i <= FORMS[f].extra; i++)
		out[i] = (char)(0x80 | ((c >> (6 * (FORMS[f].extra - i))) & 0x3f));
	return (size_t)FORMS[f].extra + 1;
}

static char unescape_simple(char c)
{
	static const char FROM[] = "\"\\/bfnrt", TO[] = "\"\\/\b\f\n\r\t";
	return TO[strchr(FROM, c) - FROM];
}

/* One character at text[k]: its code point (0 or less when invalid) and the bytes it takes. */
static long read_char(const char *text, size_t k, size_t end, size_t *step)
{
	*step = 1;
	if (text[k] != '\\')
		return (unsigned char)text[k];
	*step = 2;
	if (text[k + 1] != 'u')
		return (unsigned char)unescape_simple(text[k + 1]);
	long c = read_code_point(text + k + 1, end - k - 1, step);
	*step += 1;
	return c;
}

int copy_json_string(const char *text, const struct json_token *t, int i, char *out, size_t cap)
{
	if (i < 0 || t[i].type != JSON_STRING)
		return -1;
	size_t n = 0;
	for (size_t k = t[i].start, step = 0; k < t[i].end; k += step) {
		char utf8[4];
		int escaped = text[k] == '\\';
		long c = read_char(text, k, t[i].end, &step);
		size_t len = c <= 0 ? 0 : escaped ? encode_utf8((unsigned long)c, utf8) : (utf8[0] = (char)c, 1);
		if (!len || n + len >= cap)
			return -1;
		memcpy(out + n, utf8, len);
		n += len;
	}
	out[n] = 0;
	return 0;
}

int read_json_u64(const char *text, const struct json_token *t, int i, unsigned long long *out)
{
	if (i < 0 || t[i].type != JSON_NUMBER)
		return -1;
	unsigned long long n = 0;
	for (size_t k = t[i].start; k < t[i].end; k++) {
		unsigned d = (unsigned)(text[k] - '0');
		if (d > 9 || n > (~0ULL - d) / 10)
			return -1;
		n = n * 10 + d;
	}
	*out = n;
	return 0;
}

int read_json_bool(const char *text, const struct json_token *t, int i, int *out)
{
	if (i < 0 || t[i].type != JSON_LITERAL || text[t[i].start] == 'n')
		return -1;
	*out = text[t[i].start] == 't';
	return 0;
}
