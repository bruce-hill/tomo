#pragma once

// Integer type infos and methods

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "datatypes.h"
#include "types.h"

#define Int_t int64_t
#define Int32_t int32_t
#define Int16_t int16_t
#define Int8_t int8_t
#define I64(x) ((int64_t)x)
#define I32(x) ((int32_t)x)
#define I16(x) ((int16_t)x)
#define I8(x) ((int8_t)x)

#define DEFINE_INT_TYPE(c_type, type_name) \
    CORD type_name ## $as_text(const c_type *i, bool colorize, const TypeInfo *type); \
    int32_t type_name ## $compare(const c_type *x, const c_type *y, const TypeInfo *type); \
    bool type_name ## $equal(const c_type *x, const c_type *y, const TypeInfo *type); \
    CORD type_name ## $format(c_type i, int64_t digits); \
    CORD type_name ## $hex(c_type i, int64_t digits, bool uppercase, bool prefix); \
    CORD type_name ## $octal(c_type i, int64_t digits, bool prefix); \
    array_t type_name ## $bits(c_type x); \
    c_type type_name ## $random(int64_t min, int64_t max); \
    c_type type_name ## $from_text(CORD text, CORD *the_rest); \
    extern const c_type type_name ## $min, type_name##$max; \
    extern const TypeInfo $ ## type_name;

DEFINE_INT_TYPE(int64_t, Int);
DEFINE_INT_TYPE(int32_t, Int32);
DEFINE_INT_TYPE(int16_t, Int16);
DEFINE_INT_TYPE(int8_t,  Int8);
#undef DEFINE_INT_TYPE

#define Int$abs(...) I64(labs(__VA_ARGS__))
#define Int32$abs(...) I32(abs(__VA_ARGS__))
#define Int16$abs(...) I16(abs(__VA_ARGS__))
#define Int8$abs(...) I8(abs(__VA_ARGS__))

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
