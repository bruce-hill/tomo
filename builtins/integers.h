#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

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
    CORD type_name ## __as_str(const c_type *i, bool colorize, const TypeInfo *type); \
    int32_t type_name ## __compare(const c_type *x, const c_type *y, const TypeInfo *type); \
    CORD type_name ## __format(c_type i, int64_t digits); \
    CORD type_name ## __hex(c_type i, int64_t digits, bool uppercase, bool prefix); \
    CORD type_name ## __octal(c_type i, int64_t digits, bool prefix); \
    array_t type_name ## __bits(c_type x); \
    c_type type_name ## __random(int64_t min, int64_t max); \
    extern const c_type type_name ## __min, type_name##__max; \
    extern const TypeInfo type_name;

DEFINE_INT_TYPE(int64_t,  Int);
DEFINE_INT_TYPE(int32_t,  Int32);
DEFINE_INT_TYPE(int16_t,  Int16);
DEFINE_INT_TYPE(int8_t,   Int8);
#undef DEFINE_INT_TYPE

#define Int__abs(...) I64(labs(__VA_ARGS__))
#define Int32__abs(...) I32(abs(__VA_ARGS__))
#define Int16__abs(...) I16(abs(__VA_ARGS__))
#define Int8__abs(...) I8(abs(__VA_ARGS__))

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
