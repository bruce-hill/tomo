// Type infos and methods for floats (floating point)
// This file is a template that expects `NUMSX_C_H__BITS` to be defined before including:
//
//    #define FLOATX_C_H__BITS 64
//    #include "floatX.c.h"
//
#include <float.h>
#include <gc.h>
#include <gmp.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "fail.h"
#include "layout/int.h"
#include "layout/num.h"
#include "number.h"
#include "text.h"
#include "types.h"

#ifndef FLOATX_C_H__BITS
#define FLOATX_C_H__BITS 64
#endif

#if FLOATX_C_H__BITS == 64
#define FLOAT_T double
#define OPT_T double
#define NAMESPACED(x) Float64$##x
#define TYPE_STR "Float64"
#define SUFFIXED(x) x
#elif FLOATX_C_H__BITS == 32
#define FLOAT_T float
#define OPT_T float
#define NAMESPACED(x) Float32$##x
#define TYPE_STR "Float32"
#define SUFFIXED(x) x##f
#else
#error "Unsupported bit width for Float"
#endif

#if FLOATX_C_H__BITS == 64
#include "fpconv.h"
#include "string.h"

public
PUREFUNC Text_t NAMESPACED(value_as_text)(FLOAT_T x) {
    char *str = GC_MALLOC_ATOMIC(24);
    int len = fpconv_dtoa(x, str);
    return Text$from_strn(str, (size_t)len);
}
public
PUREFUNC Text_t NAMESPACED(as_text)(const void *x, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!x) return Text(TYPE_STR);
    static const Text_t color_prefix = Text("\x1b[35m"), color_suffix = Text("\x1b[m");
    Text_t text = NAMESPACED(value_as_text)(*(FLOAT_T *)x);
    return colorize ? Text$concat(color_prefix, text, color_suffix) : text;
}
public
PUREFUNC int32_t NAMESPACED(compare)(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    int64_t rx = *(int64_t *)x, ry = *(int64_t *)y;

    if (rx == ry) return 0;

    if (rx < 0) rx ^= INT64_MAX;
    if (ry < 0) ry ^= INT64_MAX;

    return (rx > ry) - (rx < ry);
}
#elif FLOATX_C_H__BITS == 32
public
PUREFUNC Text_t NAMESPACED(value_as_text)(FLOAT_T x) {
    return Float64$value_as_text((double)x);
}
public
PUREFUNC Text_t NAMESPACED(as_text)(const void *x, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!x) return Text(TYPE_STR);
    static const Text_t color_prefix = Text("\x1b[35m"), color_suffix = Text("\x1b[m");
    Text_t text = Float64$value_as_text((double)*(FLOAT_T *)x);
    return colorize ? Text$concat(color_prefix, text, color_suffix) : text;
}
public
PUREFUNC int32_t NAMESPACED(compare)(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    int32_t rx = *(int32_t *)x, ry = *(int32_t *)y;

    if (rx == ry) return 0;

    if (rx < 0) rx ^= INT32_MAX;
    if (ry < 0) ry ^= INT32_MAX;

    return (rx > ry) - (rx < ry);
}
#endif

// Approximating an exact real as a float is lossy for most values, since 1/3
// and sqrt(2) have no float at all, so unlike the integer conversions this
// defaults to truncate=yes: asking for a float is asking for the
// approximation. truncate=no demands the float be the exact value.
public
FLOAT_T NAMESPACED(from_num)(Num_t n, bool truncate) {
    FLOAT_T ret = (FLOAT_T)number_to_double(n);
    if unlikely (!truncate && !number_equal(number_from_double((double)ret), n))
        fail_text(Text$concat(Text("Could not convert this Num to a " TYPE_STR " without losing precision: "),
                              Text$from_str(number_to_symbolic(n))));
    return ret;
}

public
CONSTFUNC FLOAT_T NAMESPACED(from_int)(Int_t i, bool truncate) {
    if likely (i.small & 0x1) {
        FLOAT_T ret = (FLOAT_T)(i.small >> 2);
        if unlikely (!truncate && (int64_t)ret != (i.small >> 2))
            fail_text(Text$concat(Text("Could not convert integer to " TYPE_STR " without losing precision: "),
                                  Int64$value_as_text(i.small >> 2)));
        return ret;
    } else {
        FLOAT_T ret = mpz_get_d(i.big);
        if (!truncate) {
            mpz_t roundtrip;
            mpz_init_set_d(roundtrip, (double)ret);
            if unlikely (mpz_cmp(i.big, roundtrip) != 0)
                fail_text(Text$concat(Text("Could not convert integer to " TYPE_STR " without losing precision: "),
                                      Int$value_as_text(i)));
        }
        return ret;
    }
}
public
CONSTFUNC FLOAT_T NAMESPACED(from_int64)(Int64_t i, bool truncate) {
    FLOAT_T n = (FLOAT_T)i;
    if unlikely (!truncate && (Int64_t)n != i)
        fail_text(Text$concat(Text("Could not convert integer to " TYPE_STR " without losing precision: "),
                              Int64$value_as_text(i)));
    return n;
}

public
PUREFUNC bool NAMESPACED(equal)(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return *(FLOAT_T *)x == *(FLOAT_T *)y;
}

public
CONSTFUNC bool NAMESPACED(near)(FLOAT_T a, FLOAT_T b, FLOAT_T ratio, FLOAT_T absolute) {
    if (ratio < 0) ratio = 0;
    else if (ratio > 1) ratio = 1;

    if (a == b) return true;

    FLOAT_T diff = SUFFIXED(fabs)(a - b);
    if (diff < absolute) return true;
    else if (isnan(diff)) return false;

    FLOAT_T epsilon = SUFFIXED(fabs)(a * ratio) + SUFFIXED(fabs)(b * ratio);
    if (isinf(epsilon)) epsilon = DBL_MAX;
    return (diff < epsilon);
}

public
Text_t NAMESPACED(percent)(FLOAT_T x, FLOAT_T precision) {
    FLOAT_T d = SUFFIXED(100.) * x;
    d = NAMESPACED(with_precision)(d, precision);
    return Text$concat(NAMESPACED(value_as_text)(d), Text("%"));
}

public
CONSTFUNC FLOAT_T NAMESPACED(with_precision)(FLOAT_T num, FLOAT_T precision) {
    if (precision == SUFFIXED(0.0)) return num;
    // Precision will be, e.g. 0.01 or 100.
    if (precision < SUFFIXED(1.)) {
        FLOAT_T inv = SUFFIXED(round)(SUFFIXED(1.) / precision); // Necessary to make the math work
        FLOAT_T k = num * inv;
        return SUFFIXED(round)(k) / inv;
    } else {
        FLOAT_T k = num / precision;
        return SUFFIXED(round)(k) * precision;
    }
}

public
CONSTFUNC FLOAT_T NAMESPACED(mod)(FLOAT_T num, FLOAT_T modulus) {
    // Euclidean division, see:
    // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    FLOAT_T r = (FLOAT_T)remainder((double)num, (double)modulus);
    r -= (r < SUFFIXED(0.)) * (SUFFIXED(2.) * (modulus < SUFFIXED(0.)) - SUFFIXED(1.)) * modulus;
    return r;
}

public
CONSTFUNC FLOAT_T NAMESPACED(mod1)(FLOAT_T num, FLOAT_T modulus) {
    return SUFFIXED(1.0) + NAMESPACED(mod)(num - SUFFIXED(1.0), modulus);
}

public
CONSTFUNC FLOAT_T NAMESPACED(mix)(FLOAT_T amount, FLOAT_T x, FLOAT_T y) {
    return (SUFFIXED(1.0) - amount) * x + amount * y;
}

public
CONSTFUNC bool NAMESPACED(is_between)(const FLOAT_T x, const FLOAT_T low, const FLOAT_T high) {
    return (low <= x && x <= high) || (high <= x && x <= low);
}
public
CONSTFUNC FLOAT_T NAMESPACED(clamped)(FLOAT_T x, FLOAT_T low, FLOAT_T high) {
    return (x <= low) ? low : (x >= high ? high : x);
}

public
OPT_T NAMESPACED(parse)(Text_t text, Text_t *remainder) {
    const char *str = Text$as_c_string(text);
    char *end = NULL;
#if FLOATX_C_H__BITS == 64
    FLOAT_T n = strtod(str, &end);
#elif FLOATX_C_H__BITS == 32
    FLOAT_T n = strtof(str, &end);
#endif
    if (end > str) {
        if (remainder) *remainder = Text$from_str(end);
        else if (*end != '\0') return SUFFIXED(nan)("none");
        return n;
    } else {
        if (remainder) *remainder = text;
        return SUFFIXED(nan)("none");
    }
}

public
CONSTFUNC bool NAMESPACED(is_none)(const void *n, const TypeInfo_t *info) {
    (void)info;
    return isnan(*(FLOAT_T *)n);
}

// NOTE: use the isinf()/isnan() macros, not fpclassify(): the macros compile
// to inline bit checks, while fpclassify is an out-of-line libc call.
public
CONSTFUNC bool NAMESPACED(isinf)(FLOAT_T n) {
    return isinf(n);
}
public
CONSTFUNC bool NAMESPACED(finite)(FLOAT_T n) {
    return !isinf(n);
}
public
CONSTFUNC bool NAMESPACED(isnan)(FLOAT_T n) {
    return isnan(n);
}

static void NAMESPACED(set_none)(void *dest, const TypeInfo_t *type) {
    (void)type;
    *(FLOAT_T *)dest = (FLOAT_T)NAN;
}

public
const TypeInfo_t NAMESPACED(info) = {
    .size = sizeof(FLOAT_T),
    .align = __alignof__(FLOAT_T),
    .metamethods =
        {
            .compare = NAMESPACED(compare),
            .equal = NAMESPACED(equal),
            .as_text = NAMESPACED(as_text),
            .is_none = NAMESPACED(is_none),
            .set_none = NAMESPACED(set_none),
        },
};

#undef FLOAT_T
#undef OPT_T
#undef NAMESPACED
#undef TYPE_STR
#undef SUFFIXED
#undef FLOATX_C_H__BITS
