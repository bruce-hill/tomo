// Representation of the Int type and the fixed-width Int8/16/32/64 types

#pragma once

#include <stdint.h>

typedef int64_t Int64_t;
typedef int32_t Int32_t;
typedef int16_t Int16_t;
typedef int8_t Int8_t;

typedef union {
    int64_t small;
    void *big;
} Int_t;

typedef Int_t OptionalInt_t;
