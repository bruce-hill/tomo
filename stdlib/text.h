#pragma once

// Type info and methods for Text datatype, which uses a struct inspired by
// Raku's string representation and libunistr

#include <stdbool.h>
#include <printf.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "optionals.h"
#include "types.h"
#include "util.h"

#define MAX_TEXT_DEPTH 48

typedef struct {
    struct {
        Text_t text;
        int64_t offset;
    } stack[MAX_TEXT_DEPTH];
    int64_t stack_index;
} TextIter_t;

#define NEW_TEXT_ITER_STATE(t) (TextIter_t){.stack={{t, 0}}, .stack_index=0}

int printf_text(FILE *stream, const struct printf_info *info, const void *const args[]);
int printf_text_size(const struct printf_info *info, size_t n, int argtypes[n], int sizes[n]);

#define Text(str) ((Text_t){.length=sizeof(str)-1, .tag=TEXT_ASCII, .ascii="" str})

int Text$print(FILE *stream, Text_t t);
Text_t Text$_concat(int n, Text_t items[n]);
#define Text$concat(...) Text$_concat(sizeof((Text_t[]){__VA_ARGS__})/sizeof(Text_t), (Text_t[]){__VA_ARGS__})
#define Texts(...) Text$concat(__VA_ARGS__)
Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int);
Text_t Text$from(Text_t text, Int_t first);
Text_t Text$to(Text_t text, Int_t last);
Text_t Text$reversed(Text_t text);
Text_t Text$cluster(Text_t text, Int_t index_int);
OptionalText_t Text$from_str(const char *str);
OptionalText_t Text$from_strn(const char *str, size_t len);
PUREFUNC uint64_t Text$hash(const void *text, const TypeInfo_t*);
PUREFUNC int32_t Text$compare(const void *va, const void *vb, const TypeInfo_t*);
PUREFUNC bool Text$equal(const void *a, const void *b, const TypeInfo_t*);
PUREFUNC bool Text$equal_values(Text_t a, Text_t b);
PUREFUNC bool Text$equal_ignoring_case(Text_t a, Text_t b, Text_t language);
PUREFUNC bool Text$is_none(const void *t, const TypeInfo_t*);
Text_t Text$upper(Text_t text, Text_t language);
Text_t Text$lower(Text_t text, Text_t language);
Text_t Text$title(Text_t text, Text_t language);
Text_t Text$as_text(const void *text, bool colorize, const TypeInfo_t *info);
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
OptionalText_t Text$from_codepoint_names(Array_t codepoint_names);
OptionalText_t Text$from_bytes(Array_t bytes);
Array_t Text$lines(Text_t text);
Closure_t Text$by_line(Text_t text);
Text_t Text$join(Text_t glue, Array_t pieces);
Text_t Text$repeat(Text_t text, Int_t count);
Int_t Text$width(Text_t text, Text_t language);
Text_t Text$left_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
Text_t Text$right_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
Text_t Text$middle_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
int32_t Text$get_grapheme_fast(TextIter_t *state, int64_t index);
uint32_t Text$get_main_grapheme_fast(TextIter_t *state, int64_t index);
void Text$serialize(const void *obj, FILE *out, Table_t *, const TypeInfo_t *);
void Text$deserialize(FILE *in, void *out, Array_t *, const TypeInfo_t *);

MACROLIKE int32_t Text$get_grapheme(Text_t text, int64_t index)
{
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    return Text$get_grapheme_fast(&state, index);
}

extern const TypeInfo_t Text$info;
extern Text_t EMPTY_TEXT;

#define Text$metamethods ((metamethods_t){ \
    .as_text=Text$as_text, \
    .hash=Text$hash, \
    .compare=Text$compare, \
    .equal=Text$equal, \
    .is_none=Text$is_none, \
    .serialize=Text$serialize, \
    .deserialize=Text$deserialize, \
})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
