// Type info and methods for Num, Tomo's default numeric type: an exact
// computable real. See number.h for the underlying representation and
// number-design.md for its architecture.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "number.h"
#include "types.h"
#include "util.h"

// `none` is the all-zeroes word. number-design.md guarantees that is never a
// valid number -- rational zero is 0/1 with tag 01, and a heap pointer is
// never NULL -- so an optional Num costs nothing extra: no tag byte, no
// widening, same 8 bytes as a Num.
#define NONE_NUM ((OptionalNum_t){.bits = 0})

// Operators. A domain error (1/0, 0^-1, ...) fails immediately rather than
// propagating, so a Num a program can hold is never an error value.
Num_t Num$plus(Num_t x, Num_t y);
Num_t Num$minus(Num_t x, Num_t y);
Num_t Num$times(Num_t x, Num_t y);
Num_t Num$divided_by(Num_t x, Num_t y);
Num_t Num$modulo(Num_t x, Num_t y);
Num_t Num$power(Num_t base, Num_t exponent);
Num_t Num$negative(Num_t x);

// Methods that cannot fail.
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
Num_t Num$round(Num_t x);
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

Num_t Num$pi(void);
Num_t Num$tau(void);

// Approximation and conversion: a Num is exact, so getting digits out of one
// means saying how many you want.
Text_t Num$digits(Num_t n, Int_t digits);
PUREFUNC bool Num$is_exact(Num_t n, Int_t digits);
Text_t Num$symbolic(Num_t n);
Text_t Num$tex(Num_t n);
PUREFUNC Float64_t Num$to_float64(Num_t n);
PUREFUNC OptionalInt_t Num$to_int(Num_t n);
PUREFUNC bool Num$is_rational(Num_t n);
PUREFUNC bool Num$is_integer(Num_t n);

Text_t Num$as_text(const void *n, bool colorize, const TypeInfo_t *type);
Text_t Num$value_as_text(Num_t n);
PUREFUNC uint64_t Num$hash(const void *n, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare_value(Num_t x, Num_t y);
PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Num$equal_value(Num_t x, Num_t y);
PUREFUNC bool Num$is_none(const void *n, const TypeInfo_t *type);

extern const TypeInfo_t Num$info;
