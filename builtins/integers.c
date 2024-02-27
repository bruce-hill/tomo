#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../SipHash/halfsiphash.h"
#include "array.h"
#include "integers.h"
#include "types.h"
#include "string.h"

#define xstr(a) str(a)
#define str(a) #a

#define DEFINE_INT_TYPE(c_type, KindOfInt, fmt, min_val, max_val)\
    public CORD KindOfInt ## __as_str(const c_type *i, bool colorize, const TypeInfo *type) { \
        (void)type; \
        if (!i) return #KindOfInt; \
        CORD c; \
        if (colorize) CORD_sprintf(&c, "\x1b[35m%"fmt"\x1b[33;2m\x1b[m", *i); \
        else CORD_sprintf(&c, "%"fmt, *i); \
        return c; \
    } \
    public int32_t KindOfInt ## __compare(const c_type *x, const c_type *y, const TypeInfo *type) { \
        (void)type; \
        return (*x > *y) - (*x < *y); \
    } \
    public CORD KindOfInt ## __format(c_type i, int64_t digits) { \
        return CORD_asprintf("%0*" fmt, (int)digits, i); \
    } \
    public CORD KindOfInt ## __hex(c_type i, int64_t digits, bool uppercase, bool prefix) { \
        const char *hex_fmt = uppercase ? (prefix ? "0x%0.*lX" : "%0.*lX") : (prefix ? "0x%0.*lx" : "%0.*lx"); \
        return CORD_asprintf(hex_fmt, (int)digits, (uint64_t)i); \
    } \
    public CORD KindOfInt ## __octal(c_type i, int64_t digits, bool prefix) { \
        const char *octal_fmt = prefix ? "0o%0.*lo" : "%0.*lo"; \
        return CORD_asprintf(octal_fmt, (int)digits, (uint64_t)i); \
    } \
    public c_type KindOfInt ## __random(int64_t min, int64_t max) { \
        if (min > max) fail("Random min (%ld) is larger than max (%ld)", min, max); \
        if (min < (int64_t)min_val) fail("Random min (%ld) is smaller than the minimum "#KindOfInt" value", min); \
        if (max > (int64_t)max_val) fail("Random max (%ld) is smaller than the maximum "#KindOfInt" value", max); \
        int64_t range = max - min; \
        if (range > UINT32_MAX) fail("Random range (%ld) is larger than the maximum allowed (%ld)", range, UINT32_MAX); \
        uint32_t r = arc4random_uniform((uint32_t)range); \
        return min + (c_type)r; \
    } \
    public const c_type KindOfInt##__min = min_val; \
    public const c_type KindOfInt##__max = max_val; \
    public TypeInfo KindOfInt = { \
        .size=sizeof(c_type), \
        .align=__alignof__(c_type), \
        .tag=CustomInfo, \
        .CustomInfo={.compare=(void*)KindOfInt##__compare, .as_str=(void*)KindOfInt##__as_str}, \
    };

DEFINE_INT_TYPE(int64_t,  Int64,  "ld",     INT64_MIN, INT64_MAX);
DEFINE_INT_TYPE(int32_t,  Int32,  "d_i32",  INT32_MIN, INT32_MAX);
DEFINE_INT_TYPE(int16_t,  Int16,  "d_i16",  INT16_MIN, INT16_MAX);
DEFINE_INT_TYPE(int8_t,   Int8,   "d_i8",   INT8_MIN,  INT8_MAX);
#undef DEFINE_INT_TYPE

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
