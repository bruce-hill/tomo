// Representation of the Int type and the fixed-width Int8/16/32/64 types

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef int64_t Int64_t;
typedef int32_t Int32_t;
typedef int16_t Int16_t;
typedef int8_t Int8_t;

// A small Int is an immediate value tagged in its low bit; a big one is a
// pointer to a GC-allocated bigint (see bigint.h). A NULL `big` is never a
// valid pointer, so the all-zeroes word doubles as `none` and an optional Int
// is the same 8 bytes as an Int.
typedef union {
    int64_t small;
    void *big;
} Int_t;

typedef Int_t OptionalInt_t;

#define NONE_INT ((OptionalInt_t){.small = 0})

// The fixed-width integers use every bit pattern of their width, so unlike
// Int they have no spare value to spend on `none` and an optional one has to
// carry a flag alongside the value.
typedef struct {
    Int64_t value;
    bool has_value : 1;
} OptionalInt64_t;

typedef struct {
    Int32_t value;
    bool has_value : 1;
} OptionalInt32_t;

typedef struct {
    Int16_t value;
    bool has_value : 1;
} OptionalInt16_t;

typedef struct {
    Int8_t value;
    bool has_value : 1;
} OptionalInt8_t;

#define NONE_INT64 ((OptionalInt64_t){.has_value = false})
#define NONE_INT32 ((OptionalInt32_t){.has_value = false})
#define NONE_INT16 ((OptionalInt16_t){.has_value = false})
#define NONE_INT8 ((OptionalInt8_t){.has_value = false})
