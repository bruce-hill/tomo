// Template header for 64 and 32 bit floats
// This file expects `FLOATX_H__BITS` to be defined before including:
//
//     #define FLOATX_H__BITS 64
//     #include "floatX.h"
//

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

#ifndef FLOATX_H__BITS
#define FLOATX_H__BITS 64
#endif

#if FLOATX_H__BITS == 64
#define FLOAT_T double
#define OPT_T double
#define NAMESPACED(x) Float64$##x
#define TYPE_STR "Float64"
#define SUFFIXED(x) x
#elif FLOATX_H__BITS == 32
#define FLOAT_T float
#define OPT_T float
#define NAMESPACED(x) Float32$##x
#define TYPE_STR "Float32"
#define SUFFIXED(x) x##f
#else
#error "Unsupported bit width for Float"
#endif

Text_t NAMESPACED(as_text)(const void *x, bool colorize, const TypeInfo_t *type);
Text_t NAMESPACED(value_as_text)(FLOAT_T x);
PUREFUNC int32_t NAMESPACED(compare)(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool NAMESPACED(equal)(const void *x, const void *y, const TypeInfo_t *type);
CONSTFUNC bool NAMESPACED(near)(FLOAT_T a, FLOAT_T b, FLOAT_T ratio, FLOAT_T absolute);
Text_t NAMESPACED(percent)(FLOAT_T x, FLOAT_T precision);
FLOAT_T CONSTFUNC NAMESPACED(with_precision)(FLOAT_T num, FLOAT_T precision);
FLOAT_T NAMESPACED(mod)(FLOAT_T num, FLOAT_T modulus);
FLOAT_T NAMESPACED(mod1)(FLOAT_T num, FLOAT_T modulus);
CONSTFUNC bool NAMESPACED(isinf)(FLOAT_T n);
CONSTFUNC bool NAMESPACED(finite)(FLOAT_T n);
CONSTFUNC bool NAMESPACED(isnan)(FLOAT_T n);
bool NAMESPACED(is_none)(const void *n, const TypeInfo_t *info);
FLOAT_T NAMESPACED(nan)(Text_t tag);
CONSTFUNC FLOAT_T NAMESPACED(mix)(FLOAT_T amount, FLOAT_T x, FLOAT_T y);
OPT_T NAMESPACED(parse)(Text_t text, Text_t *remainder);
CONSTFUNC bool NAMESPACED(is_between)(const FLOAT_T x, const FLOAT_T low, const FLOAT_T high);
CONSTFUNC FLOAT_T NAMESPACED(clamped)(FLOAT_T x, FLOAT_T low, FLOAT_T high);

#if FLOATX_H__BITS == 64
MACROLIKE CONSTFUNC FLOAT_T NAMESPACED(from_float32)(float n) {
    return (FLOAT_T)n;
}
#elif FLOATX_H__BITS == 32
MACROLIKE CONSTFUNC FLOAT_T NAMESPACED(from_float64)(double n) {
    return (FLOAT_T)n;
}
#endif

CONSTFUNC FLOAT_T NAMESPACED(from_int)(Int_t i, bool truncate);
CONSTFUNC FLOAT_T NAMESPACED(from_int64)(Int64_t i, bool truncate);
MACROLIKE CONSTFUNC FLOAT_T NAMESPACED(from_int32)(Int32_t i) {
    return (FLOAT_T)i;
}
MACROLIKE CONSTFUNC FLOAT_T NAMESPACED(from_int16)(Int16_t i) {
    return (FLOAT_T)i;
}
MACROLIKE CONSTFUNC FLOAT_T NAMESPACED(from_int8)(Int8_t i) {
    return (FLOAT_T)i;
}
MACROLIKE CONSTFUNC FLOAT_T NAMESPACED(from_byte)(Byte_t i) {
    return (FLOAT_T)i;
}

extern const TypeInfo_t NAMESPACED(info);

#undef FLOAT_T
#undef OPT_T
#undef NAMESPACED
#undef TYPE_STR
#undef SUFFIXED
#undef FLOATX_H__BITS
