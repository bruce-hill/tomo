// Integer type infos and methods
// This file expects `INTX_H__INT_BITS` to be defined before including:
//
//     #define INTX_H__INT_BITS 64
//     #include "intX.h"
//
#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#ifndef INTX_H__INT_BITS
#define INTX_H__INT_BITS 32
#endif

#define PASTE3_(a, b, c) a##b##c
#define PASTE3(a, b, c) PASTE3_(a, b, c)
#define INTX_T PASTE3(int, INTX_H__INT_BITS, _t)
#define UINTX_T PASTE3(uint, INTX_H__INT_BITS, _t)

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)
#define NAME_STR "Int" STRINGIFY(INTX_H__INT_BITS)

#define OPT_T PASTE3(OptionalInt, INTX_H__INT_BITS, _t)

#define PASTE4_(a, b, c, d) a##b##c##d
#define PASTE4(a, b, c, d) PASTE4_(a, b, c, d)
#define NAMESPACED(method_name) PASTE4(Int, INTX_H__INT_BITS, $, method_name)

typedef struct {
    INTX_T value;
    bool has_value : 1;
} OPT_T;

Text_t NAMESPACED(as_text)(const void *i, bool colorize, const TypeInfo_t *type);
Text_t NAMESPACED(value_as_text)(INTX_T i);
PUREFUNC int32_t NAMESPACED(compare)(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool NAMESPACED(equal)(const void *x, const void *y, const TypeInfo_t *type);
Text_t NAMESPACED(hex)(INTX_T i, Int_t digits, bool uppercase, bool prefix);
Text_t NAMESPACED(octal)(INTX_T i, Int_t digits, bool prefix);
List_t NAMESPACED(bits)(INTX_T x);
bool NAMESPACED(get_bit)(INTX_T x, Int_t bit_index);
Closure_t NAMESPACED(to)(INTX_T first, INTX_T last, OPT_T step);
Closure_t NAMESPACED(onward)(INTX_T first, INTX_T step);
PUREFUNC OPT_T NAMESPACED(parse)(Text_t text, Text_t *remainder);
CONSTFUNC bool NAMESPACED(is_between)(const INTX_T x, const INTX_T low, const INTX_T high);
CONSTFUNC INTX_T NAMESPACED(clamped)(INTX_T x, INTX_T min, INTX_T max);
MACROLIKE CONSTFUNC INTX_T NAMESPACED(from_byte)(Byte_t b) { return (INTX_T)b; }
MACROLIKE CONSTFUNC INTX_T NAMESPACED(from_bool)(Bool_t b) { return (INTX_T)b; }
CONSTFUNC INTX_T NAMESPACED(gcd)(INTX_T x, INTX_T y);
extern const INTX_T NAMESPACED(min), NAMESPACED(max);
extern const TypeInfo_t NAMESPACED(info);

MACROLIKE INTX_T NAMESPACED(abs)(INTX_T x) {
#if INTX_H__INT_BITS >= 64
    return (INTX_T)labs(x);
#else
    return (INTX_T)abs(x);
#endif
}

MACROLIKE INTX_T NAMESPACED(divided_by)(INTX_T D, INTX_T d) {
    INTX_T q = D / d, r = D % d;
    q -= (r < 0) * (2 * (d > 0) - 1);
    return q;
}

MACROLIKE INTX_T NAMESPACED(modulo)(INTX_T D, INTX_T d) {
    INTX_T r = D % d;
    r -= (r < 0) * (2 * (d < 0) - 1) * d;
    return r;
}

MACROLIKE INTX_T NAMESPACED(modulo1)(INTX_T D, INTX_T d) { return NAMESPACED(modulo)(D - 1, d) + 1; }

MACROLIKE PUREFUNC INTX_T NAMESPACED(wrapping_plus)(INTX_T x, INTX_T y) { return (INTX_T)((UINTX_T)x + (UINTX_T)y); }

MACROLIKE PUREFUNC INTX_T NAMESPACED(wrapping_minus)(INTX_T x, INTX_T y) { return (INTX_T)((UINTX_T)x + (UINTX_T)y); }

MACROLIKE PUREFUNC INTX_T NAMESPACED(unsigned_left_shifted)(INTX_T x, INTX_T y) { return (INTX_T)((UINTX_T)x << y); }

MACROLIKE PUREFUNC INTX_T NAMESPACED(unsigned_right_shifted)(INTX_T x, INTX_T y) { return (INTX_T)((UINTX_T)x >> y); }

void NAMESPACED(serialize)(const void *obj, FILE *out, Table_t *, const TypeInfo_t *);
void NAMESPACED(deserialize)(FILE *in, void *outval, List_t *, const TypeInfo_t *);

MACROLIKE PUREFUNC INTX_T NAMESPACED(from_num)(Num_t n, bool truncate) {
    INTX_T i = (INTX_T)n;
    if (!truncate && unlikely((Num_t)i != n)) fail("Could not convert Num to an " NAME_STR " without truncation: ", n);
    return i;
}

MACROLIKE PUREFUNC INTX_T NAMESPACED(from_num32)(Num32_t n, bool truncate) {
    INTX_T i = (INTX_T)n;
    if (!truncate && unlikely((Num32_t)i != n))
        fail("Could not convert Num32 to an " NAME_STR " without truncation: ", n);
    return i;
}

MACROLIKE PUREFUNC INTX_T NAMESPACED(from_int)(Int_t i, bool truncate) {
    if likely (i.small & 1L) {
        INTX_T ret = i.small >> 2L;
#if INTX_H__INT_BITS < 32
        if (!truncate && unlikely((int64_t)ret != (i.small >> 2L)))
            fail("Integer is too big to fit in an " NAME_STR ": ", i);
#endif
        return ret;
    }
    if (!truncate && unlikely(!mpz_fits_slong_p(i.big))) fail("Integer is too big to fit in an " NAME_STR ": ", i);
    return mpz_get_si(i.big);
}

#if INTX_H__INT_BITS < 64
MACROLIKE PUREFUNC INTX_T NAMESPACED(from_int64)(Int64_t i64, bool truncate) {
    INTX_T i = (INTX_T)i64;
    if (!truncate && unlikely((int64_t)i != i64)) fail("Integer is too big to fit in an " NAME_STR ": ", i64);
    return i;
}
#elif INTX_H__INT_BITS > 64
MACROLIKE CONSTFUNC INTX_T NAMESPACED(from_int64)(Int64_t i) { return (INTX_T)i; }
#endif

#if INTX_H__INT_BITS < 32
MACROLIKE PUREFUNC INTX_T NAMESPACED(from_int32)(Int32_t i32, bool truncate) {
    INTX_T i = (INTX_T)i32;
    if (!truncate && unlikely((int32_t)i != i32)) fail("Integer is too big to fit in an " NAME_STR ": ", i32);
    return i;
}
#elif INTX_H__INT_BITS > 32
MACROLIKE CONSTFUNC INTX_T NAMESPACED(from_int32)(Int32_t i) { return (INTX_T)i; }
#endif

#if INTX_H__INT_BITS < 16
MACROLIKE PUREFUNC INTX_T NAMESPACED(from_int16)(Int16_t i16, bool truncate) {
    INTX_T i = (INTX_T)i16;
    if (!truncate && unlikely((int16_t)i != i16)) fail("Integer is too big to fit in an " NAME_STR ": ", i16);
    return i;
}
#elif INTX_H__INT_BITS > 16
MACROLIKE CONSTFUNC INTX_T NAMESPACED(from_int16)(Int16_t i) { return (INTX_T)i; }
#endif

#if INTX_H__INT_BITS > 8
MACROLIKE CONSTFUNC INTX_T NAMESPACED(from_int8)(Int8_t i) { return (INTX_T)i; }
#endif

#undef PASTE3_
#undef PASTE3
#undef INTX_T
#undef STRINGIFY_
#undef STRINGIFY
#undef NAME_STR
#undef UINTX_T
#undef OPT_T
#undef PASTE4_
#undef PASTE4
#undef NAMESPACED
#undef INTX_H__INT_BITS
