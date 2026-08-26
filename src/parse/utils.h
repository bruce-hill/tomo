// Some common parsing utilities
#pragma once

#include <gc.h>
#include <string.h>

#include <stdbool.h>

#include "../util.h"
#include "context.h"

#define SPACES_PER_INDENT 4

CONSTFUNC bool is_keyword(const char *word);
size_t some_of(const char **pos, const char *allow);
size_t some_not(const char **pos, const char *forbid);
size_t spaces(const char **pos);
void whitespace(parse_ctx_t *ctx, const char **pos);
size_t match(const char **pos, const char *target);
size_t match_word(const char **pos, const char *word);
const char *get_word(const char **pos);
const char *get_id(const char **pos);
bool comment(parse_ctx_t *ctx, const char **pos);
bool indent(parse_ctx_t *ctx, const char **pos);
const char *eol(const char *str);
PUREFUNC int64_t get_indent(parse_ctx_t *ctx, const char *pos);
const char *unescape(parse_ctx_t *ctx, const char **out, size_t *len_out);
bool is_xid_start_next(const char *pos);
bool is_xid_continue_next(const char *pos);
bool newline_with_indentation(const char **out, int64_t target);
bool match_separator(parse_ctx_t *ctx, const char **pos);

// Zig's libc strspn()/strcspn() measure the *whole* string with strlen() before
// matching anything, to turn the null-terminated pointers into slices. On a
// pointer into a source file that makes every call cost O(bytes to end of file)
// however few characters it actually consumes -- which turned skipping one
// newline between statements into a scan of the entire rest of the file, and
// made parsing quadratic in file length. These scan only what they consume.
PUREFUNC MACROLIKE bool char_in_set(char c, const char *set) {
    for (; *set; set++)
        if (*set == c) return true;
    return false;
}

PUREFUNC MACROLIKE size_t span_of(const char *s, const char *accept) {
    const char *p = s;
    while (*p && char_in_set(*p, accept))
        p += 1;
    return (size_t)(p - s);
}

PUREFUNC MACROLIKE size_t span_not(const char *s, const char *reject) {
    const char *p = s;
    while (*p && !char_in_set(*p, reject))
        p += 1;
    return (size_t)(p - s);
}

// GC_strndup() calls strlen() on its argument before clamping to `len`, so
// copying a few characters out of a source file measures the whole rest of the
// file first -- the same trap as strspn() above. This copies only `len` bytes.
MACROLIKE char *strndup_bounded(const char *s, size_t len) {
    char *copy = GC_MALLOC_ATOMIC(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}
