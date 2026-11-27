/*
 * A small JSON reader for the package manifests: the text is split into tokens once, and values are
 * looked up and copied out by index. No allocation; the text stays the caller's.
 */
#pragma once
#include <stddef.h>

enum json_type { JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_NUMBER, JSON_LITERAL };

struct json_token {
	enum json_type type;
	size_t start, end; /* the value's bytes; a string's without its quotes */
	int count;         /* object: key/value pairs; array: items */
	int next;          /* index of the first token after this value and everything inside it */
};

/* Number of tokens (the first is the whole document), or -1 when the text is not one JSON value or
 * needs more than max tokens. */
int parse_json(const char *text, size_t len, struct json_token *tokens, int max);
/* The value of key in the object at index obj, or -1. */
int find_json_key(const char *text, const struct json_token *t, int obj, const char *key);
/* Item i of the array at index arr, or -1. */
int find_json_item(const struct json_token *t, int arr, int i);
/* The string at index i, unescaped to UTF-8: -1 when it is not a string, holds a NUL or does not fit. */
int copy_json_string(const char *text, const struct json_token *t, int i, char *out, size_t cap);
/* A non-negative integer: -1 for anything else. */
int read_json_u64(const char *text, const struct json_token *t, int i, unsigned long long *out);
/* true or false: -1 for anything else. */
int read_json_bool(const char *text, const struct json_token *t, int i, int *out);
