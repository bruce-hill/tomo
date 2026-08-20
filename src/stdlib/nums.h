// Type info and methods for Num, Tomo's default numeric type: an exact
// computable real. See number.h for the underlying representation and
// number-design.md for its architecture.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "number.h"
#include "types.h"
#include "util.h"

// `none` is the all-zeroes word. number-design.md guarantees that is never a
// valid number -- rational zero is 0/1 with tag 01, and a heap pointer is
// never NULL -- so an optional Num costs nothing extra: no tag byte, no
// widening, same 8 bytes as a Num.
#define NONE_NUM ((OptionalNum_t){.bits = 0})

Text_t Num$as_text(const void *n, bool colorize, const TypeInfo_t *type);
Text_t Num$value_as_text(Num_t n);
PUREFUNC uint64_t Num$hash(const void *n, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare_value(Num_t x, Num_t y);
PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Num$equal_value(Num_t x, Num_t y);
PUREFUNC bool Num$is_none(const void *n, const TypeInfo_t *type);

extern const TypeInfo_t Num$info;
