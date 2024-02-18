#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define DEFINE_INT_TYPE(c_type, type_name) \
    CORD type_name ## __as_str(const c_type *i, bool colorize, const TypeInfo *type); \
    int32_t type_name ## __compare(const c_type *x, const c_type *y, const TypeInfo *type); \
    CORD type_name ## __format(c_type i, int64_t digits); \
    CORD type_name ## __hex(c_type i, int64_t digits, bool uppercase, bool prefix); \
    CORD type_name ## __octal(c_type i, int64_t digits, bool prefix); \
    c_type type_name ## __random(int64_t min, int64_t max); \
    typedef struct { \
        TypeInfo type; \
        c_type min, max; \
        c_type (*abs)(c_type i); \
        CORD (*format)(c_type i, int64_t digits); \
        CORD (*hex)(c_type i, int64_t digits, bool uppercase, bool prefix); \
        CORD (*octal)(c_type i, int64_t digits, bool prefix); \
        c_type (*random)(int64_t min, int64_t max); \
    } type_name##_namespace_t; \
    extern type_name##_namespace_t type_name##_type;

DEFINE_INT_TYPE(int64_t,  Int);
DEFINE_INT_TYPE(int64_t,  Int64);
DEFINE_INT_TYPE(int32_t,  Int32);
DEFINE_INT_TYPE(int16_t,  Int16);
DEFINE_INT_TYPE(int8_t,   Int8);
#undef DEFINE_INT_TYPE

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
