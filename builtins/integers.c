#include <gc.h>
#include <gc/cord.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../SipHash/halfsiphash.h"
#include "array.h"
#include "types.h"
#include "string.h"

extern const void *SSS_HASH_VECTOR;

#define xstr(a) str(a)
#define str(a) #a

#define DEFINE_INT_TYPE(c_type, KindOfInt, fmt, abs_fn, min_val, max_val)\
    public CORD KindOfInt ## __cord(const c_type *i, bool colorize, const TypeInfo *type) { \
        (void)type; \
        CORD c; \
        if (colorize) CORD_sprintf(&c, "\x1b[35m%"fmt"\x1b[33;2m\x1b[m", *i); \
        else CORD_sprintf(&c, "%"fmt, *i); \
        return c; \
    } \
    public int32_t KindOfInt ## __compare(const c_type *x, const c_type *y, const TypeInfo *type) { \
        (void)type; \
        return (*x > *y) - (*x < *y); \
    } \
    public Str_t KindOfInt ## __format(c_type i, int64_t digits) { \
        int len = snprintf(NULL, 0, "%0*" fmt, (int)digits, i); \
        char *str = GC_MALLOC_ATOMIC(len + 1); \
        snprintf(str, len+1, "%0*" fmt, (int)digits, i); \
        return (Str_t){.data=str, .length=len, .stride=1}; \
    } \
    public Str_t KindOfInt ## __hex(c_type i, int64_t digits, bool uppercase, bool prefix) { \
        const char *hex_fmt = uppercase ? (prefix ? "0x%0.*lX" : "%0.*lX") : (prefix ? "0x%0.*lx" : "%0.*lx"); \
        int len = snprintf(NULL, 0, hex_fmt, (int)digits, (uint64_t)i); \
        char *str = GC_MALLOC_ATOMIC(len + 1); \
        snprintf(str, len+1, hex_fmt, (int)digits, (uint64_t)i); \
        return (Str_t){.data=str, .length=len, .stride=1}; \
    } \
    public Str_t KindOfInt ## __octal(c_type i, int64_t digits, bool prefix) { \
        const char *octal_fmt = prefix ? "0o%0.*lo" : "%0.*lo"; \
        int len = snprintf(NULL, 0, octal_fmt, (int)digits, (uint64_t)i); \
        char *str = GC_MALLOC_ATOMIC(len + 1); \
        snprintf(str, len+1, octal_fmt, (int)digits, (uint64_t)i); \
        return (Str_t){.data=str, .length=len, .stride=1}; \
    } \
    public c_type KindOfInt ## __random(int64_t min, int64_t max) { \
        if (min > max) builtin_fail("Random min (%ld) is larger than max (%ld)", min, max); \
        if (min < (int64_t)min_val) builtin_fail("Random min (%ld) is smaller than the minimum "#KindOfInt" value", min); \
        if (max > (int64_t)max_val) builtin_fail("Random max (%ld) is smaller than the maximum "#KindOfInt" value", max); \
        int64_t range = max - min; \
        if (range > UINT32_MAX) builtin_fail("Random range (%ld) is larger than the maximum allowed (%ld)", range, UINT32_MAX); \
        uint32_t r = arc4random_uniform((uint32_t)range); \
        return min + (c_type)r; \
    } \
    public struct { \
        TypeInfo type; \
        c_type min, max; \
        c_type (*abs)(c_type i); \
        Str_t (*format)(c_type i, int64_t digits); \
        Str_t (*hex)(c_type i, int64_t digits, bool uppercase, bool prefix); \
        Str_t (*octal)(c_type i, int64_t digits, bool prefix); \
        c_type (*random)(int64_t min, int64_t max); \
    } KindOfInt##_type = { \
        .type={ \
            .name=#KindOfInt, \
            .size=sizeof(c_type), \
            .align=alignof(c_type), \
            .tag=CustomInfo, \
            .CustomInfo={.compare=(void*)KindOfInt##__compare, .cord=(void*)KindOfInt##__cord}, \
        }, \
        .min=min_val, \
        .max=max_val, \
        .abs=(void*)abs_fn, \
        .format=KindOfInt##__format, \
        .hex=KindOfInt##__hex, \
        .octal=KindOfInt##__octal, \
        .random=KindOfInt##__random, \
    };

DEFINE_INT_TYPE(int64_t,  Int,    "ld",     labs, INT64_MIN, INT64_MAX);
DEFINE_INT_TYPE(int32_t,  Int32,  "d_i32",  abs,  INT32_MIN, INT32_MAX);
DEFINE_INT_TYPE(int16_t,  Int16,  "d_i16",  abs,  INT16_MIN, INT16_MAX);
DEFINE_INT_TYPE(int8_t,   Int8,   "d_i8",   abs,  INT8_MIN,  INT8_MAX);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
