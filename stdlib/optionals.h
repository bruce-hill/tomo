#pragma once

// Optional types

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

#define OptionalBool_t uint8_t
#define OptionalArray_t Array_t
#define OptionalTable_t Table_t
#define OptionalText_t Text_t
#define OptionalClosure_t Closure_t

#define NONE_ARRAY ((Array_t){.length=-1})
#define NONE_BOOL ((OptionalBool_t)2)
#define NONE_INT ((OptionalInt_t){.small=0})
#define NONE_TABLE ((OptionalTable_t){.entries.length=-1})
#define NONE_CLOSURE ((OptionalClosure_t){.fn=NULL})
#define NONE_TEXT ((OptionalText_t){.length=-1})
#define NONE_MOMENT ((OptionalMoment_t){.tv_usec=-1})

PUREFUNC bool is_null(const void *obj, const TypeInfo_t *non_optional_type);
Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo_t *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
