#pragma once

// Type info and methods for Text datatype, which uses a struct inspired by
// Raku's string representation and libunistr

#include <stdbool.h>
#include <printf.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"

int printf_text(FILE *stream, const struct printf_info *info, const void *const args[]);
int printf_text_size(const struct printf_info *info, size_t n, int argtypes[n], int sizes[n]);

#define Text(str) ((Text_t){.length=sizeof(str)-1, .tag=TEXT_ASCII, .ascii="" str})

int Text$print(FILE *stream, Text_t t);
void Text$visualize(Text_t t);
Text_t Text$_concat(int n, Text_t items[n]);
#define Text$concat(...) Text$_concat(sizeof((Text_t[]){__VA_ARGS__})/sizeof(Text_t), (Text_t[]){__VA_ARGS__})
Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int);
Text_t Text$from_str(const char *str);
uint64_t Text$hash(Text_t *text);
int32_t Text$compare(const Text_t *a, const Text_t *b);
bool Text$equal(const Text_t *a, const Text_t *b);
bool Text$equal_ignoring_case(Text_t a, Text_t b);
Text_t Text$upper(Text_t text);
Text_t Text$lower(Text_t text);
Text_t Text$title(Text_t text);
Text_t Text$as_text(const void *text, bool colorize, const TypeInfo *info);
Text_t Text$quoted(Text_t str, bool colorize);
Text_t Text$replace(Text_t str, Pattern_t pat, Text_t replacement);
array_t Text$split(Text_t text, Pattern_t pattern);
Int_t Text$find(Text_t text, Pattern_t pattern, Int_t i, int64_t *match_length);
array_t Text$find_all(Text_t text, Pattern_t pattern);
bool Text$has(Text_t text, Pattern_t pattern);
const char *Text$as_c_string(Text_t text);
public Text_t Text$format(const char *fmt, ...);
array_t Text$clusters(Text_t text);
array_t Text$utf32_codepoints(Text_t text);
array_t Text$utf8_bytes(Text_t text);
array_t Text$codepoint_names(Text_t text);
Text_t Text$from_codepoints(array_t codepoints);
Text_t Text$from_codepoint_names(array_t codepoint_names);
Text_t Text$from_bytes(array_t bytes);
array_t Text$lines(Text_t text);
Text_t Text$join(Text_t glue, array_t pieces);

extern const TypeInfo $Text;

Pattern_t Pattern$escape_text(Text_t text);
extern const TypeInfo Pattern;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
