#pragma once

// The type representing text patterns for pattern matching.

#include <stdbool.h>
#include <printf.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "optionals.h"
#include "types.h"

#define Pattern(text) ((Pattern_t)Text(text))
#define Patterns(...) ((Pattern_t)Texts(__VA_ARGS__))

typedef struct {
    Text_t text;
    Int_t index;
    Array_t captures;
} Match_t;

typedef Match_t OptionalMatch_t;
#define NULL_MATCH ((OptionalMatch_t){.index=NULL_INT})

Text_t Text$replace(Text_t str, Pattern_t pat, Text_t replacement, Pattern_t backref_pat, bool recursive);
Pattern_t Pattern$escape_text(Text_t text);
Text_t Text$replace_all(Text_t text, Table_t replacements, Pattern_t backref_pat, bool recursive);
Array_t Text$split(Text_t text, Pattern_t pattern);
Text_t Text$trim(Text_t text, Pattern_t pattern, bool trim_left, bool trim_right);
OptionalMatch_t Text$find(Text_t text, Pattern_t pattern, Int_t i);
Array_t Text$find_all(Text_t text, Pattern_t pattern);
PUREFUNC bool Text$has(Text_t text, Pattern_t pattern);
OptionalArray_t Text$matches(Text_t text, Pattern_t pattern);
Text_t Text$map(Text_t text, Pattern_t pattern, Closure_t fn);

#define Pattern$hash Text$hash
#define Pattern$compare Text$compare
#define Pattern$equal Text$equal

extern const TypeInfo_t Match$info;
extern const TypeInfo_t Pattern$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
