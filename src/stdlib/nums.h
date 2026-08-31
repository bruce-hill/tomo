// Type info and methods for Num, Tomo's default numeric type: an exact
// computable real. See number.h for the underlying representation and
// number-design.md for its architecture.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "layout/bool.h"
#include "layout/byte.h"
#include "layout/float.h"
#include "layout/int.h"
#include "layout/num.h" // IWYU pragma: export
#include "number.h"
#include "types.h"
#include "util.h"

// --- Operators ---
//
// Defined inline, for the same reason Int$plus is (see bigint.h): these are
// the hot path, and the part worth duplicating at a call site is small. The
// arithmetic they wrap is itself an inline fast path over the immediate tier
// falling through to an out-of-line general case (see number.h), so an
// operator on two small rationals, the overwhelmingly common case,
// compiles to a few instructions in the caller with no call at all.
//
// A domain error (1/0, 0^-1, ...) fails immediately rather than propagating,
// so a Num a program can hold is never an error value. The reporting is
// out-of-line and noreturn, which keeps the inline body to one predicted-
// not-taken branch.

_Noreturn void Num$arithmetic_error(Num_t bad);

// These three cannot produce an error from valid operands, so they carry no
// check whatsoever.
MACROLIKE Num_t Num$plus(Num_t x, Num_t y) {
    return number_add(x, y);
}
MACROLIKE Num_t Num$minus(Num_t x, Num_t y) {
    return number_sub(x, y);
}
MACROLIKE Num_t Num$times(Num_t x, Num_t y) {
    return number_mul(x, y);
}
MACROLIKE Num_t Num$negative(Num_t x) {
    return number_neg(x);
}

MACROLIKE Num_t Num$divided_by(Num_t x, Num_t y) { // y == 0
    Num_t result = number_div(x, y);
    if (unlikely(NUMBER_IS_ERROR(result))) Num$arithmetic_error(result);
    return result;
}
// Out-of-line (nums.c): Euclidean, built on floor_divided_by, so it's not a
// single-call wrapper like the others here.
Num_t Num$modulo(Num_t x, Num_t y); // y == 0
MACROLIKE Num_t Num$power(Num_t base, Num_t exponent) { // 0^negative, negative^non-integer
    Num_t result = number_pow(base, exponent);
    if (unlikely(NUMBER_IS_ERROR(result))) Num$arithmetic_error(result);
    return result;
}

// Methods that cannot fail.
Num_t Num$floor_divided_by(Num_t x, Num_t y);
Num_t Num$abs(Num_t x);
Num_t Num$sin(Num_t x);
Num_t Num$cos(Num_t x);
Num_t Num$atan(Num_t x);
Num_t Num$sinh(Num_t x);
Num_t Num$cosh(Num_t x);
Num_t Num$tanh(Num_t x);
Num_t Num$exp(Num_t x);

// Rounding (see nums.c: fails only for an irrational the library can't place
// on one side of the boundary, which no symbolic form ever hits).
Num_t Num$floor(Num_t x);
Num_t Num$ceil(Num_t x);
Num_t Num$round(Num_t x, Num_t increment);
Num_t Num$trunc(Num_t x);

// Methods with a restricted domain: out of domain is `none`.
OptionalNum_t Num$sqrt(Num_t x);
OptionalNum_t Num$log(Num_t x);
OptionalNum_t Num$log10(Num_t x);
OptionalNum_t Num$log2(Num_t x);
OptionalNum_t Num$tan(Num_t x);
OptionalNum_t Num$asin(Num_t x);
OptionalNum_t Num$acos(Num_t x);
OptionalNum_t Num$inverse(Num_t x);
OptionalNum_t Num$atan2(Num_t y, Num_t x);
OptionalNum_t Num$gcd(Num_t x, Num_t y);
OptionalNum_t Num$lcm(Num_t x, Num_t y);

PUREFUNC Num_t Num$clamped(Num_t x, Num_t low, Num_t high);
PUREFUNC bool Num$is_between(Num_t x, Num_t low, Num_t high);
PUREFUNC Num_t Num$min(Num_t x, Num_t y);
PUREFUNC Num_t Num$max(Num_t x, Num_t y);
Num_t Num$mix(Num_t amount, Num_t x, Num_t y);
Text_t Num$percent(Num_t n, Num_t precision);

PUREFUNC Num_t Num$from_bool(Bool_t b);
PUREFUNC Num_t Num$from_byte(Byte_t b);
PUREFUNC Num_t Num$from_int8(Int8_t i);
PUREFUNC Num_t Num$from_int16(Int16_t i);
PUREFUNC Num_t Num$from_int32(Int32_t i);
PUREFUNC Num_t Num$from_int64(Int64_t i);
Num_t Num$from_int(Int_t i);
PUREFUNC Num_t Num$from_float64(Float64_t n);
PUREFUNC Num_t Num$from_float32(Float32_t n);
OptionalNum_t Num$parse(Text_t text);

Num_t Num$pi(void);
Num_t Num$tau(void);

// Approximation and conversion: a Num is exact, so getting digits out of one
// means saying how many you want.
Text_t Num$digits(Num_t n, Int_t digits, Text_t ellipsis);
PUREFUNC bool Num$is_exact(Num_t n, Int_t digits);
Text_t Num$symbolic(Num_t n);
Text_t Num$tex(Num_t n);
PUREFUNC bool Num$is_rational(Num_t n);
PUREFUNC bool Num$is_integer(Num_t n);

Text_t Num$as_text(const void *n, bool colorize, const TypeInfo_t *type);
Text_t Num$value_as_text(Num_t n);
PUREFUNC uint64_t Num$hash(const void *n, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC int32_t Num$undecided_compare(Num_t x, Num_t y);

// Where exactness runs out. Comparing two irrationals is undecidable in
// general, since proving sqrt(3 + 2*sqrt(2)) == 1 + sqrt(2) takes symbolic
// reasoning no engine does in full, so past this many digits of agreement,
// two values are treated as the same. Deep enough that nothing a program
// actually computes lands inside it by accident.
#define EQUALITY_DIGITS 40

// EQUALITY_DIGITS expressed as bits of refinement, which is what the exact
// layer actually works in: 40 decimal places need 40*log2(10) = 132.9 bits,
// and refinement decides a value once its magnitude exceeds 4 at the working
// precision, i.e. once |x| > 2^-(w-2), so w = 140 resolves everything
// down to 2^-138 ~= 2.3e-42, comfortably past the last digit that matters.
//
// This is the number that makes the two halves of Num equality line up. The
// undecided fallback below rounds to EQUALITY_DIGITS and compares that, so
// any refinement past this width produces an answer that gets thrown away
// and, worse, is the *expensive* part: proving a difference nonzero gets
// harder the smaller it is, and a difference that is exactly zero can never
// be proven nonzero at all, so an uncapped call always runs every doubling
// to the internal cap before giving up. Capping here means "agrees to 40
// digits" is the whole rule, rather than a fallback that only applies when
// several thousand digits of refinement happened to come up empty.
#define NUM_EQUALITY_PREC 140

// number_compare answers 2 for "couldn't decide", which happens only for two
// general irrationals; everything else, every rational and every closed
// symbolic form, decides here, inline.
MACROLIKE PUREFUNC int32_t Num$compare_value(Num_t x, Num_t y) {
    int cmp = number_compare_capped(x, y, NUM_EQUALITY_PREC);
    if (unlikely(cmp == 2)) return Num$undecided_compare(x, y);
    return (int32_t)cmp;
}
PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *type);
// When BOTH sides are small rational immediates, bit equality settles it
// outright: small rationals are canonical, so equal ones are bit-identical.
// Unlike number_equal in number.h, whose equality is exactly the library's
// own, so a *single* tag test suffices there, equality here is whatever
// Num$compare_value says, and that widens to "agrees to EQUALITY_DIGITS" for
// an undecidable irrational (see Num$undecided_compare). A one-sided test
// would let `sin(1):asin()! == 1.0` answer no while `<=` and `>=` both answer
// yes, and while Num$hash puts the two in the same bucket.
MACROLIKE PUREFUNC bool Num$equal_value(Num_t x, Num_t y) {
    if (likely((x.bits & 0x3) == 0x1 && (y.bits & 0x3) == 0x1)) return x.bits == y.bits;
    return Num$compare_value(x, y) == 0;
}
PUREFUNC bool Num$is_none(const void *n, const TypeInfo_t *type);

extern const TypeInfo_t Num$info;
