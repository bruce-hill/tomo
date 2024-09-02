#pragma once

// Type info and methods for Text datatype, which uses a struct inspired by
// Raku's string representation and libunistr

#include <stdbool.h>
#include <printf.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "where.h"

typedef struct {
    enum { FIND_FAILURE, FIND_SUCCESS } status;
    int32_t index;
} find_result_t;

// CORD Text$as_text(const void *str, bool colorize, const TypeInfo *info);
// CORD Text$quoted(CORD str, bool colorize);
// // int Text$compare(const CORD *x, const CORD *y);
// // bool Text$equal(const CORD *x, const CORD *y);
// // uint32_t Text$hash(const CORD *cord);
// // CORD Text$upper(CORD str);
// // CORD Text$lower(CORD str);
// // CORD Text$title(CORD str);
// bool Text$has(CORD str, CORD target, Where_t where);
// CORD Text$without(CORD str, CORD target, Where_t where);
// CORD Text$trimmed(CORD str, CORD skip, Where_t where);
// find_result_t Text$find(CORD str, CORD pat);
// CORD Text$replace(CORD text, CORD pat, CORD replacement, Int_t limit);
// array_t Text$split(CORD str, CORD split);
// CORD Text$join(CORD glue, array_t pieces);
// array_t Text$clusters(CORD text);
// array_t Text$codepoints(CORD text);
// array_t Text$bytes(CORD text);
// Int_t Text$num_clusters(CORD text);
// Int_t Text$num_codepoints(CORD text);
// Int_t Text$num_bytes(CORD text);
// array_t Text$character_names(CORD text);
// CORD Text$read_line(CORD prompt);

int printf_text(FILE *stream, const struct printf_info *info, const void *const args[]);
int printf_text_size(const struct printf_info *info, size_t n, int argtypes[n], int sizes[n]);

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
Text_t Text$replace(Text_t str, Text_t pat, Text_t replacement);
Int_t Text$find(Text_t text, Text_t pattern, Int_t i, int64_t *match_length);
const char *Text$as_c_string(Text_t text);
public Text_t Text$format(const char *fmt, ...);

extern const TypeInfo $Text;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
