// Type info and methods for Text datatype, which uses a struct inspired by
// Raku's string representation and libunistr

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h" // IWYU pragma: export
#include "mapmacro.h"
#include "nums.h" // IWYU pragma: export
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

#define NEW_TEXT_ITER_STATE(t) (TextIter_t){.stack = {{t, 0}}, .stack_index = 0}

#define Text(str) ((Text_t){.length = sizeof(str) - 1, .tag = TEXT_ASCII, .ascii = "" str})

static inline Text_t Text_from_str_literal(const char *str) {
    return (Text_t){.length = strlen(str), .tag = TEXT_ASCII, .ascii = str};
}

static inline Text_t Text_from_text(Text_t t) { return t; }

#define convert_to_text(x)                                                                                             \
    _Generic(x,                                                                                                        \
        Text_t: Text_from_text,                                                                                        \
        char *: Text$from_str,                                                                                         \
        const char *: Text$from_str,                                                                                   \
        int8_t: Int8$value_as_text,                                                                                    \
        int16_t: Int16$value_as_text,                                                                                  \
        int32_t: Int32$value_as_text,                                                                                  \
        int64_t: Int64$value_as_text,                                                                                  \
        double: Num$value_as_text,                                                                                     \
        float: Num32$value_as_text)(x)

Text_t Text$_concat(int n, Text_t items[n]);
#define Text$concat(...) Text$_concat(sizeof((Text_t[]){__VA_ARGS__}) / sizeof(Text_t), (Text_t[]){__VA_ARGS__})
#define Texts(...) Text$concat(MAP_LIST(convert_to_text, __VA_ARGS__))
// int Text$print(FILE *stream, Text_t t);
Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int);
Text_t Text$from(Text_t text, Int_t first);
Text_t Text$to(Text_t text, Int_t last);
Text_t Text$reversed(Text_t text);
OptionalText_t Text$cluster(Text_t text, Int_t index_int);
#define Text$cluster_checked(text_expr, index_expr, start, end)                                                        \
    ({                                                                                                                 \
        const Text_t text = text_expr;                                                                                 \
        Int_t index = index_expr;                                                                                      \
        OptionalText_t cluster = Text$cluster(text, index);                                                            \
        if (unlikely(cluster.tag == TEXT_NONE))                                                                        \
            fail_source(__SOURCE_FILE__, start, end, "Invalid text index: ", index, " (text has length ",              \
                        (int64_t)text.length, ")\n");                                                                  \
        cluster;                                                                                                       \
    })
OptionalText_t Text$from_str(const char *str);
OptionalText_t Text$from_strn(const char *str, size_t len);
PUREFUNC uint64_t Text$hash(const void *text, const TypeInfo_t *);
PUREFUNC int32_t Text$compare(const void *va, const void *vb, const TypeInfo_t *);
PUREFUNC bool Text$equal(const void *a, const void *b, const TypeInfo_t *);
PUREFUNC bool Text$equal_values(Text_t a, Text_t b);
PUREFUNC bool Text$equal_ignoring_case(Text_t a, Text_t b, Text_t language);
PUREFUNC bool Text$is_none(const void *t, const TypeInfo_t *);
Text_t Text$upper(Text_t text, Text_t language);
Text_t Text$lower(Text_t text, Text_t language);
Text_t Text$title(Text_t text, Text_t language);
Text_t Text$as_text(const void *text, bool colorize, const TypeInfo_t *info);
Text_t Text$escaped(Text_t text, bool colorize, Text_t extra_escapes);
Text_t Text$quoted(Text_t str, bool colorize, Text_t quotation_mark);
PUREFUNC bool Text$starts_with(Text_t text, Text_t prefix, Text_t *remainder);
PUREFUNC bool Text$ends_with(Text_t text, Text_t suffix, Text_t *remainder);
Text_t Text$without_prefix(Text_t text, Text_t prefix);
Text_t Text$without_suffix(Text_t text, Text_t suffix);
Text_t Text$replace(Text_t text, Text_t target, Text_t replacement);
Text_t Text$translate(Text_t text, Table_t translations);
PUREFUNC bool Text$has(Text_t text, Text_t target);
List_t Text$split(Text_t text, Text_t delimiter);
List_t Text$split_any(Text_t text, Text_t delimiters);
Closure_t Text$by_split(Text_t text, Text_t delimiter);
Closure_t Text$by_split_any(Text_t text, Text_t delimiters);
Text_t Text$trim(Text_t text, Text_t to_trim, bool left, bool right);
char *Text$as_c_string(Text_t text);
List_t Text$clusters(Text_t text);
List_t Text$utf8(Text_t text);
List_t Text$utf16(Text_t text);
List_t Text$utf32(Text_t text);
List_t Text$codepoint_names(Text_t text);
OptionalText_t Text$from_utf8(List_t units);
OptionalText_t Text$from_utf16(List_t units);
OptionalText_t Text$from_utf32(List_t codepoints);
OptionalText_t Text$from_codepoint_names(List_t codepoint_names);
List_t Text$lines(Text_t text);
Closure_t Text$by_line(Text_t text);
Text_t Text$join(Text_t glue, List_t pieces);
Text_t Text$repeat(Text_t text, Int_t count);
Int_t Text$width(Text_t text, Text_t language);
Text_t Text$left_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
Text_t Text$right_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
Text_t Text$middle_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
int32_t Text$get_grapheme_fast(TextIter_t *state, int64_t index);
uint32_t Text$get_main_grapheme_fast(TextIter_t *state, int64_t index);
Int_t Text$memory_size(Text_t text);
Text_t Text$layout(Text_t text);
void Text$serialize(const void *obj, FILE *out, Table_t *, const TypeInfo_t *);
void Text$deserialize(FILE *in, void *out, List_t *, const TypeInfo_t *);

MACROLIKE int32_t Text$get_grapheme(Text_t text, int64_t index) {
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    return Text$get_grapheme_fast(&state, index);
}

extern const TypeInfo_t Text$info;
extern Text_t EMPTY_TEXT;

#define Text$metamethods                                                                                               \
    {                                                                                                                  \
        .as_text = Text$as_text,                                                                                       \
        .hash = Text$hash,                                                                                             \
        .compare = Text$compare,                                                                                       \
        .equal = Text$equal,                                                                                           \
        .is_none = Text$is_none,                                                                                       \
        .serialize = Text$serialize,                                                                                   \
        .deserialize = Text$deserialize,                                                                               \
    }
