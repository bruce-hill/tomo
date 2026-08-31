// Representation of the Num type

#pragma once

#include <stdint.h>

// A Num is an exact computable real: a 64-bit tagged value whose low bits say
// which tier it is (see number.h, and number-design.md for the full design).
// The all-zeroes word is never a valid one, so it doubles as `none` and an
// optional Num is the same 8 bytes as a Num.
//
// The `Num_s` tag is not redundant with the typedef: it is the name gdb hands
// back for a value carrying no typedef, and tomo-gdb.py turns a `_s` suffix
// into the matching `Num$info`.
typedef struct Num_s {
    uint64_t bits;
} Num_t;

typedef Num_t OptionalNum_t;
