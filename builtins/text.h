#pragma once

// Type info and methods for Text datatype, which uses a struct inspired by
// Raku's string representation and libunistr

#include <stdbool.h>
#include <printf.h>
#include <stdint.h>
#include <unistr.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"

typedef struct {
    int64_t subtext, sum_of_previous_subtexts;
} TextIter_t;

int printf_text(FILE *stream, const struct printf_info *info, const void *const args[]);
int printf_text_size(const struct printf_info *info, size_t n, int argtypes[n], int sizes[n]);

#define Text(str) ((Text_t){.length=sizeof(str)-1, .tag=TEXT_ASCII, .ascii="" str})

int Text$print(FILE *stream, Text_t t);
void Text$visualize(Text_t t);
Text_t Text$_concat(int n, Text_t items[n]);
#define Text$concat(...) Text$_concat(sizeof((Text_t[]){__VA_ARGS__})/sizeof(Text_t), (Text_t[]){__VA_ARGS__})
#define Texts(...) Text$concat(__VA_ARGS__)
Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int);
Text_t Text$from_str(const char *str);
Text_t Text$from_strn(const char *str, size_t len);
PUREFUNC uint64_t Text$hash(Text_t *text);
PUREFUNC int32_t Text$compare(const Text_t *a, const Text_t *b);
PUREFUNC bool Text$equal(const Text_t *a, const Text_t *b);
PUREFUNC bool Text$equal_values(Text_t a, Text_t b);
PUREFUNC bool Text$equal_ignoring_case(Text_t a, Text_t b);
Text_t Text$upper(Text_t text);
Text_t Text$lower(Text_t text);
Text_t Text$title(Text_t text);
Text_t Text$as_text(const void *text, bool colorize, const TypeInfo *info);
Text_t Text$quoted(Text_t str, bool colorize);
PUREFUNC bool Text$starts_with(Text_t text, Text_t prefix);
PUREFUNC bool Text$ends_with(Text_t text, Text_t suffix);
char *Text$as_c_string(Text_t text);
__attribute__((format(printf, 1, 2)))
public Text_t Text$format(const char *fmt, ...);
Array_t Text$clusters(Text_t text);
Array_t Text$utf32_codepoints(Text_t text);
Array_t Text$utf8_bytes(Text_t text);
Array_t Text$codepoint_names(Text_t text);
Text_t Text$from_codepoints(Array_t codepoints);
Text_t Text$from_codepoint_names(Array_t codepoint_names);
Text_t Text$from_bytes(Array_t bytes);
Array_t Text$lines(Text_t text);
Text_t Text$join(Text_t glue, Array_t pieces);
Text_t Text$repeat(Text_t text, Int_t count);
int32_t Text$get_grapheme_fast(Text_t text, TextIter_t *state, int64_t index);
ucs4_t Text$get_main_grapheme_fast(Text_t text, TextIter_t *state, int64_t index);

static inline int32_t Text$get_grapheme(Text_t text, int64_t index)
{
    TextIter_t state = {0, 0};
    return Text$get_grapheme_fast(text, &state, index);
}

extern const TypeInfo Text$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
