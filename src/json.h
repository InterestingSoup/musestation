/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_JSON_H
#define MUSE_JSON_H
#include <stddef.h>
#include <stdint.h>
struct muse_json_token { size_t start, end; int next; char type; };
struct muse_json { const char *data; size_t length; int count; struct muse_json_token tokens[512]; };
/* Strict JSON, max 32 nesting levels / 512 tokens; duplicate keys rejected. */
int muse_json_parse(struct muse_json *j, const void *data, size_t length);
/* Parse one strict JSON value from a bounded prefix; caller validates delimiters
 * and the remainder. Keeps the same depth/token/duplicate-key bounds. */
int muse_json_parse_prefix(struct muse_json *j, const void *data, size_t length, size_t *consumed);
int muse_json_get(const struct muse_json *j, int object, const char *key);
int muse_json_string(const struct muse_json *j, int token, char *out, size_t size);
int muse_json_u64(const struct muse_json *j, int token, uint64_t *out);
int muse_json_bool(const struct muse_json *j, int token, int *out);
/* Includes enclosing quotes; returns 0 on success. */
int muse_json_quote(const char *input, char *output, size_t capacity);
#endif
