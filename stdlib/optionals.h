#pragma once

// Optional types

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

#define OptionalBool_t uint8_t
#define OptionalArray_t Array_t
#define OptionalTable_t Table_t
#define OptionalText_t Text_t
#define OptionalClosure_t Closure_t

extern const OptionalBool_t NULL_BOOL;
extern const OptionalTable_t NULL_TABLE;
extern const OptionalArray_t NULL_ARRAY;
extern const OptionalInt_t NULL_INT;
extern const OptionalClosure_t NULL_CLOSURE;
extern const OptionalText_t NULL_TEXT;

PUREFUNC bool is_null(const void *obj, const TypeInfo *non_optional_type);
Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
