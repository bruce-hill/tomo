// Template header for 64 and 32 bit Nums
// This file expects `NUMX_H__BITS` to be defined before including:
//
//     #define NUMX_H__BITS 64
//     #include "numX.h"
//

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#ifndef NUMX_H__BITS
#define NUMX_H__BITS 64
#endif

#if NUMX_H__BITS == 64
#define NUM_T double
#define OPT_T double
#define NAMESPACED(x) Num$##x
#define TYPE_STR "Num"
#define SUFFIXED(x) x
#elif NUMX_H__BITS == 32
#define NUM_T float
#define OPT_T float
#define NAMESPACED(x) Num32$##x
#define TYPE_STR "Num32"
#define SUFFIXED(x) x##f
#else
#error "Unsupported bit width for Num"
#endif

Text_t NAMESPACED(as_text)(const void *x, bool colorize, const TypeInfo_t *type);
Text_t NAMESPACED(value_as_text)(NUM_T x);
PUREFUNC int32_t NAMESPACED(compare)(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool NAMESPACED(equal)(const void *x, const void *y, const TypeInfo_t *type);
CONSTFUNC bool NAMESPACED(near)(NUM_T a, NUM_T b, NUM_T ratio, NUM_T absolute);
Text_t NAMESPACED(percent)(NUM_T x, NUM_T precision);
NUM_T CONSTFUNC NAMESPACED(with_precision)(NUM_T num, NUM_T precision);
NUM_T NAMESPACED(mod)(NUM_T num, NUM_T modulus);
NUM_T NAMESPACED(mod1)(NUM_T num, NUM_T modulus);
CONSTFUNC bool NAMESPACED(isinf)(NUM_T n);
CONSTFUNC bool NAMESPACED(finite)(NUM_T n);
CONSTFUNC bool NAMESPACED(isnan)(NUM_T n);
bool NAMESPACED(is_none)(const void *n, const TypeInfo_t *info);
NUM_T NAMESPACED(nan)(Text_t tag);
CONSTFUNC NUM_T NAMESPACED(mix)(NUM_T amount, NUM_T x, NUM_T y);
OPT_T NAMESPACED(parse)(Text_t text, Text_t *remainder);
CONSTFUNC bool NAMESPACED(is_between)(const NUM_T x, const NUM_T low, const NUM_T high);
CONSTFUNC NUM_T NAMESPACED(clamped)(NUM_T x, NUM_T low, NUM_T high);

#if NUMX_H__BITS == 64
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_num32)(float n) { return (NUM_T)n; }
#elif NUMX_H__BITS == 32
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_num64)(double n) { return (NUM_T)n; }
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_int)(Int_t i, bool truncate) {
    if likely (i.small & 0x1) {
        NUM_T ret = (NUM_T)(i.small >> 2);
        if unlikely (!truncate && (int64_t)ret != (i.small >> 2))
            fail("Could not convert integer to " TYPE_STR " without losing precision: ", i.small >> 2);
        return ret;
    } else {
        NUM_T ret = mpz_get_d(*i.big);
        if (!truncate) {
            mpz_t roundtrip;
            mpz_init_set_d(roundtrip, ret);
            if unlikely (mpz_cmp(*i.big, roundtrip) != 0)
                fail("Could not convert integer to " TYPE_STR " without losing precision: ", i);
        }
        return ret;
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_int64)(Int64_t i, bool truncate) {
    NUM_T n = (NUM_T)i;
    if unlikely (!truncate && (Int64_t)n != i)
        fail("Could not convert integer to " TYPE_STR " without losing precision: ", i);
    return n;
}
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_int32)(Int32_t i) { return (NUM_T)i; }
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_int16)(Int16_t i) { return (NUM_T)i; }
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_int8)(Int8_t i) { return (NUM_T)i; }
MACROLIKE CONSTFUNC NUM_T NAMESPACED(from_byte)(Byte_t i) { return (NUM_T)i; }

extern const TypeInfo_t NAMESPACED(info);

#undef NUM_T
#undef OPT_T
#undef NAMESPACED
#undef TYPE_STR
#undef SUFFIXED
#undef NUMX_H__BITS
