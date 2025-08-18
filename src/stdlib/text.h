#pragma once

// Type info and methods for Text datatype, which uses a struct inspired by
// Raku's string representation and libunistr

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "mapmacro.h"
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

#define Text(str) ((Text_t){.length=sizeof(str)-1, .tag=TEXT_ASCII, .ascii="" str})

static inline Text_t Text_from_str_literal(const char *str) {
    return (Text_t){.length=strlen(str), .tag=TEXT_ASCII, .ascii=str};
}

static inline Text_t Text_from_text(Text_t t) {
    return t;
}

#define convert_to_text(x) _Generic(x, Text_t: Text_from_text, char*: Textヽfrom_str, const char*: Textヽfrom_str)(x)

Text_t Textヽ_concat(int n, Text_t items[n]);
#define Textヽconcat(...) Textヽ_concat(sizeof((Text_t[]){__VA_ARGS__})/sizeof(Text_t), (Text_t[]){__VA_ARGS__})
#define Texts(...) Textヽconcat(MAP_LIST(convert_to_text, __VA_ARGS__))
// int Textヽprint(FILE *stream, Text_t t);
Text_t Textヽslice(Text_t text, Int_t first_int, Int_t last_int);
Text_t Textヽfrom(Text_t text, Int_t first);
Text_t Textヽto(Text_t text, Int_t last);
Text_t Textヽreversed(Text_t text);
Text_t Textヽcluster(Text_t text, Int_t index_int);
OptionalText_t Textヽfrom_str(const char *str);
OptionalText_t Textヽfrom_strn(const char *str, size_t len);
PUREFUNC uint64_t Textヽhash(const void *text, const TypeInfo_t*);
PUREFUNC int32_t Textヽcompare(const void *va, const void *vb, const TypeInfo_t*);
PUREFUNC bool Textヽequal(const void *a, const void *b, const TypeInfo_t*);
PUREFUNC bool Textヽequal_values(Text_t a, Text_t b);
PUREFUNC bool Textヽequal_ignoring_case(Text_t a, Text_t b, Text_t language);
PUREFUNC bool Textヽis_none(const void *t, const TypeInfo_t*);
Text_t Textヽupper(Text_t text, Text_t language);
Text_t Textヽlower(Text_t text, Text_t language);
Text_t Textヽtitle(Text_t text, Text_t language);
Text_t Textヽas_text(const void *text, bool colorize, const TypeInfo_t *info);
Text_t Textヽquoted(Text_t str, bool colorize, Text_t quotation_mark);
PUREFUNC bool Textヽstarts_with(Text_t text, Text_t prefix, Text_t *remainder);
PUREFUNC bool Textヽends_with(Text_t text, Text_t suffix, Text_t *remainder);
Text_t Textヽwithout_prefix(Text_t text, Text_t prefix);
Text_t Textヽwithout_suffix(Text_t text, Text_t suffix);
Text_t Textヽreplace(Text_t text, Text_t target, Text_t replacement);
Text_t Textヽtranslate(Text_t text, Table_t translations);
PUREFUNC bool Textヽhas(Text_t text, Text_t target);
List_t Textヽsplit(Text_t text, Text_t delimiter);
List_t Textヽsplit_any(Text_t text, Text_t delimiters);
Closure_t Textヽby_split(Text_t text, Text_t delimiter);
Closure_t Textヽby_split_any(Text_t text, Text_t delimiters);
Text_t Textヽtrim(Text_t text, Text_t to_trim, bool left, bool right);
char *Textヽas_c_string(Text_t text);
List_t Textヽclusters(Text_t text);
List_t Textヽutf32_codepoints(Text_t text);
List_t Textヽutf8_bytes(Text_t text);
List_t Textヽcodepoint_names(Text_t text);
Text_t Textヽfrom_codepoints(List_t codepoints);
OptionalText_t Textヽfrom_codepoint_names(List_t codepoint_names);
OptionalText_t Textヽfrom_bytes(List_t bytes);
List_t Textヽlines(Text_t text);
Closure_t Textヽby_line(Text_t text);
Text_t Textヽjoin(Text_t glue, List_t pieces);
Text_t Textヽrepeat(Text_t text, Int_t count);
Int_t Textヽwidth(Text_t text, Text_t language);
Text_t Textヽleft_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
Text_t Textヽright_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
Text_t Textヽmiddle_pad(Text_t text, Int_t width, Text_t padding, Text_t language);
int32_t Textヽget_grapheme_fast(TextIter_t *state, int64_t index);
uint32_t Textヽget_main_grapheme_fast(TextIter_t *state, int64_t index);
Int_t Textヽmemory_size(Text_t text);
Text_t Textヽlayout(Text_t text);
void Textヽserialize(const void *obj, FILE *out, Table_t *, const TypeInfo_t *);
void Textヽdeserialize(FILE *in, void *out, List_t *, const TypeInfo_t *);

MACROLIKE int32_t Textヽget_grapheme(Text_t text, int64_t index)
{
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    return Textヽget_grapheme_fast(&state, index);
}

extern const TypeInfo_t Textヽinfo;
extern Text_t EMPTY_TEXT;

#define Textヽmetamethods { \
    .as_text=Textヽas_text, \
    .hash=Textヽhash, \
    .compare=Textヽcompare, \
    .equal=Textヽequal, \
    .is_none=Textヽis_none, \
    .serialize=Textヽserialize, \
    .deserialize=Textヽdeserialize, \
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
