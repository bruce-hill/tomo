// Integer type infos and methods
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "datatypes.h"
#include "integers.h"
#include "string.h"
#include "types.h"

#define xstr(a) str(a)
#define str(a) #a

#define DEFINE_INT_TYPE(c_type, KindOfInt, fmt, min_val, max_val)\
    public CORD KindOfInt ## $as_text(const c_type *i, bool colorize, const TypeInfo *type) { \
        (void)type; \
        if (!i) return #KindOfInt; \
        CORD c; \
        if (colorize) CORD_sprintf(&c, "\x1b[35m%"fmt"\x1b[33;2m\x1b[m", *i); \
        else CORD_sprintf(&c, "%"fmt, *i); \
        return c; \
    } \
    public int32_t KindOfInt ## $compare(const c_type *x, const c_type *y, const TypeInfo *type) { \
        (void)type; \
        return (*x > *y) - (*x < *y); \
    } \
    public bool KindOfInt ## $equal(const c_type *x, const c_type *y, const TypeInfo *type) { \
        (void)type; \
        return *x == *y; \
    } \
    public CORD KindOfInt ## $format(c_type i, int64_t digits) { \
        return CORD_asprintf("%0*" fmt, (int)digits, i); \
    } \
    public CORD KindOfInt ## $hex(c_type i, int64_t digits, bool uppercase, bool prefix) { \
        const char *hex_fmt = uppercase ? (prefix ? "0x%0.*lX" : "%0.*lX") : (prefix ? "0x%0.*lx" : "%0.*lx"); \
        return CORD_asprintf(hex_fmt, (int)digits, (uint64_t)i); \
    } \
    public CORD KindOfInt ## $octal(c_type i, int64_t digits, bool prefix) { \
        const char *octal_fmt = prefix ? "0o%0.*lo" : "%0.*lo"; \
        return CORD_asprintf(octal_fmt, (int)digits, (uint64_t)i); \
    } \
    public array_t KindOfInt ## $bits(c_type x) { \
        array_t bit_array = (array_t){.data=GC_MALLOC_ATOMIC(sizeof(bool[8*sizeof(c_type)])), .atomic=1, .stride=sizeof(bool), .length=8*sizeof(c_type)}; \
        bool *bits = bit_array.data + sizeof(c_type)*8; \
        for (size_t i = 0; i < 8*sizeof(c_type); i++) { \
            *(bits--) = x & 1; \
            x >>= 1; \
        } \
        return bit_array; \
    } \
    public c_type KindOfInt ## $random(c_type min, c_type max) { \
        if (min > max) fail("Random minimum value (%ld) is larger than the maximum value (%ld)", min, max); \
        if (min == max) return min; \
        if (min == min_val && max == max_val) { \
            c_type r; \
            arc4random_buf(&r, sizeof(r)); \
            return r; \
        } \
        uint64_t range = (uint64_t)max - (uint64_t)min + 1; \
        uint64_t min_r = -range % range; \
        uint64_t r; \
        for (;;) { \
            arc4random_buf(&r, sizeof(r)); \
            if (r >= min_r) break; \
        } \
        return (c_type)((uint64_t)min + (r % range)); \
    } \
    public Range_t KindOfInt ## $to(c_type from, c_type to) { \
        return (Range_t){from, to, to >= from ? 1 : -1}; \
    } \
    public c_type KindOfInt ## $from_text(CORD text, CORD *the_rest) { \
        const char *str = CORD_to_const_char_star(text); \
        long i; \
        char *end_ptr = NULL; \
        if (strncmp(str, "0x", 2) == 0) { \
            i = strtol(str, &end_ptr, 16); \
        } else if (strncmp(str, "0o", 2) == 0) { \
            i = strtol(str, &end_ptr, 8); \
        } else if (strncmp(str, "0b", 2) == 0) { \
            i = strtol(str, &end_ptr, 2); \
        } else { \
            i = strtol(str, &end_ptr, 10); \
        } \
        if (the_rest) *the_rest = CORD_from_char_star(end_ptr); \
        if (i < min_val) i = min_val; \
        else if (i > max_val) i = min_val; \
        return (c_type)i; \
    } \
    public const c_type KindOfInt##$min = min_val; \
    public const c_type KindOfInt##$max = max_val; \
    public const TypeInfo $ ## KindOfInt = { \
        .size=sizeof(c_type), \
        .align=__alignof__(c_type), \
        .tag=CustomInfo, \
        .CustomInfo={.compare=(void*)KindOfInt##$compare, .as_text=(void*)KindOfInt##$as_text}, \
    };

DEFINE_INT_TYPE(int64_t,  Int,    "ld",     INT64_MIN, INT64_MAX);
DEFINE_INT_TYPE(int32_t,  Int32,  "d_i32",  INT32_MIN, INT32_MAX);
DEFINE_INT_TYPE(int16_t,  Int16,  "d_i16",  INT16_MIN, INT16_MAX);
DEFINE_INT_TYPE(int8_t,   Int8,   "d_i8",   INT8_MIN,  INT8_MAX);
#undef DEFINE_INT_TYPE

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
