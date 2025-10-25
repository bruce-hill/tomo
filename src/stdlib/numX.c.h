// Type infos and methods for Nums (floating point)
// This file is a template that expects `NUMSX_C_H__BITS` to be defined before including:
//
//    #define NUMX_C_H__BITS 64
//    #include "numX.c.h"
//
#include <float.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "text.h"
#include "types.h"

#ifndef NUMX_C_H__BITS
#define NUMX_C_H__BITS 64
#endif

#if NUMX_C_H__BITS == 64
#define NUM_T double
#define OPT_T double
#define NAMESPACED(x) Num$##x
#define TYPE_STR "Num"
#define SUFFIXED(x) x
#elif NUMX_C_H__BITS == 32
#define NUM_T float
#define OPT_T float
#define NAMESPACED(x) Num32$##x
#define TYPE_STR "Num32"
#define SUFFIXED(x) x##f
#else
#error "Unsupported bit width for Num"
#endif

#if NUMX_C_H__BITS == 64
#include "fpconv.h"
#include "string.h"

public
PUREFUNC Text_t NAMESPACED(value_as_text)(NUM_T x) {
    char *str = GC_MALLOC_ATOMIC(24);
    int len = fpconv_dtoa(x, str);
    return Text$from_strn(str, (size_t)len);
}
public
PUREFUNC Text_t NAMESPACED(as_text)(const void *x, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!x) return Text(TYPE_STR);
    static const Text_t color_prefix = Text("\x1b[35m"), color_suffix = Text("\x1b[m");
    Text_t text = NAMESPACED(value_as_text)(*(NUM_T *)x);
    return colorize ? Texts(color_prefix, text, color_suffix) : text;
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
#elif NUMX_C_H__BITS == 32
public
PUREFUNC Text_t NAMESPACED(value_as_text)(NUM_T x) { return Num$value_as_text((double)x); }
PUREFUNC Text_t NAMESPACED(as_text)(const void *x, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!x) return Text(TYPE_STR);
    static const Text_t color_prefix = Text("\x1b[35m"), color_suffix = Text("\x1b[m");
    Text_t text = Num$value_as_text((double)*(NUM_T *)x);
    return colorize ? Texts(color_prefix, text, color_suffix) : text;
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

public
PUREFUNC bool NAMESPACED(equal)(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return *(NUM_T *)x == *(NUM_T *)y;
}

public
CONSTFUNC bool NAMESPACED(near)(NUM_T a, NUM_T b, NUM_T ratio, NUM_T absolute) {
    if (ratio < 0) ratio = 0;
    else if (ratio > 1) ratio = 1;

    if (a == b) return true;

    NUM_T diff = SUFFIXED(fabs)(a - b);
    if (diff < absolute) return true;
    else if (isnan(diff)) return false;

    NUM_T epsilon = SUFFIXED(fabs)(a * ratio) + SUFFIXED(fabs)(b * ratio);
    if (isinf(epsilon)) epsilon = DBL_MAX;
    return (diff < epsilon);
}

public
Text_t NAMESPACED(percent)(NUM_T x, NUM_T precision) {
    NUM_T d = SUFFIXED(100.) * x;
    d = NAMESPACED(with_precision)(d, precision);
    return Texts(NAMESPACED(value_as_text)(d), Text("%"));
}

public
CONSTFUNC NUM_T NAMESPACED(with_precision)(NUM_T num, NUM_T precision) {
    if (precision == SUFFIXED(0.0)) return num;
    // Precision will be, e.g. 0.01 or 100.
    if (precision < SUFFIXED(1.)) {
        NUM_T inv = SUFFIXED(round)(SUFFIXED(1.) / precision); // Necessary to make the math work
        NUM_T k = num * inv;
        return SUFFIXED(round)(k) / inv;
    } else {
        NUM_T k = num / precision;
        return SUFFIXED(round)(k) * precision;
    }
}

public
CONSTFUNC NUM_T NAMESPACED(mod)(NUM_T num, NUM_T modulus) {
    // Euclidean division, see:
    // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    NUM_T r = (NUM_T)remainder((double)num, (double)modulus);
    r -= (r < SUFFIXED(0.)) * (SUFFIXED(2.) * (modulus < SUFFIXED(0.)) - SUFFIXED(1.)) * modulus;
    return r;
}

public
CONSTFUNC NUM_T NAMESPACED(mod1)(NUM_T num, NUM_T modulus) {
    return SUFFIXED(1.0) + NAMESPACED(mod)(num - SUFFIXED(1.0), modulus);
}

public
CONSTFUNC NUM_T NAMESPACED(mix)(NUM_T amount, NUM_T x, NUM_T y) { return (SUFFIXED(1.0) - amount) * x + amount * y; }

public
CONSTFUNC bool NAMESPACED(is_between)(const NUM_T x, const NUM_T low, const NUM_T high) {
    return low <= x && x <= high;
}
public
CONSTFUNC NUM_T NAMESPACED(clamped)(NUM_T x, NUM_T low, NUM_T high) {
    return (x <= low) ? low : (x >= high ? high : x);
}

public
OPT_T NAMESPACED(parse)(Text_t text, Text_t *remainder) {
    const char *str = Text$as_c_string(text);
    char *end = NULL;
#if NUMX_C_H__BITS == 64
    NUM_T n = strtod(str, &end);
#elif NUMX_C_H__BITS == 32
    NUM_T n = strtof(str, &end);
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
    return isnan(*(NUM_T *)n);
}

public
CONSTFUNC bool NAMESPACED(isinf)(NUM_T n) { return (fpclassify(n) == FP_INFINITE); }
public
CONSTFUNC bool NAMESPACED(finite)(NUM_T n) { return (fpclassify(n) != FP_INFINITE); }
public
CONSTFUNC bool NAMESPACED(isnan)(NUM_T n) { return (fpclassify(n) == FP_NAN); }

public
const TypeInfo_t NAMESPACED(info) = {
    .size = sizeof(NUM_T),
    .align = __alignof__(NUM_T),
    .metamethods =
        {
            .compare = NAMESPACED(compare),
            .equal = NAMESPACED(equal),
            .as_text = NAMESPACED(as_text),
            .is_none = NAMESPACED(is_none),
        },
};

#undef NUM_T
#undef OPT_T
#undef NAMESPACED
#undef TYPE_STR
#undef SUFFIXED
#undef NUMX_C_H__BITS
