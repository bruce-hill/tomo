#pragma once

// Optional types

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

#define OptionalBool_t uint8_t

extern const OptionalBool_t NULL_BOOL;
extern const Table_t NULL_TABLE;
extern const Array_t NULL_ARRAY;
extern const Int_t NULL_INT;
extern const Closure_t NULL_CLOSURE;
extern const Text_t NULL_TEXT;

PUREFUNC bool is_null(const void *obj, const TypeInfo *non_optional_type);
Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
