#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define Num64_t double
#define Num32_t float
#define Num_t double

CORD Num64__as_str(const double *f, bool colorize, const TypeInfo *type);
int32_t Num64__compare(const double *x, const double *y, const TypeInfo *type);
bool Num64__equal(const double *x, const double *y, const TypeInfo *type);
CORD Num64__format(double f, int64_t precision);
CORD Num64__scientific(double f, int64_t precision);
double Num64__mod(double num, double modulus);
bool Num64__isinf(double n);
bool Num64__finite(double n);
bool Num64__isnan(double n);

typedef bool (*double_pred_t)(double);
typedef double (*double_unary_fn_t)(double);
typedef double (*double_binary_fn_t)(double, double);

typedef struct {
    TypeInfo type;
    // Constants:
    double NaN, _2_sqrt_pi, e, half_pi, inf, inverse_half_pi, inverse_pi, ln10, ln2,
           log2e, pi, quarter_pi, sqrt2, sqrt_half, tau;
    // Nullary functions:
    double (*random)(void);
    // Predicates:
    double_pred_t finite, isinf, isnan;
    // Unary functions:
    double_unary_fn_t abs, acos, acosh, asin, asinh, atan, atanh, cbrt, ceil, cos, cosh, erf, erfc,
                      exp, exp10, exp2, expm1, floor, j0, j1, log, log10, log1p, log2, logb,
                      nextdown, nextup, rint, round, roundeven, significand, sin, sinh, sqrt,
                      tan, tanh, tgamma, trunc, y0, y1;
    // Binary functions:
    double_binary_fn_t atan2, copysign, dist, hypot, maxmag, minmag, mod, nextafter, pow, remainder;
    // Odds and ends:
    CORD (*format)(double f, int64_t precision);
    CORD (*scientific)(double f, int64_t precision);
} Num64_namespace_t;
extern Num64_namespace_t Num64;

CORD Num32__as_str(float *f, bool colorize, const TypeInfo *type);
int32_t Num32__compare(const float *x, const float *y, const TypeInfo *type);
bool Num32__equal(const float *x, const float *y, const TypeInfo *type);
CORD Num32__format(float f, int64_t precision);
CORD Num32__scientific(float f, int64_t precision);
float Num32__mod(float num, float modulus);
float Num32__random(void);
bool Num32__isinf(float n);
bool Num32__finite(float n);
bool Num32__isnan(float n);

typedef bool (*float_pred_t)(float);
typedef float (*float_unary_fn_t)(float);
typedef float (*float_binary_fn_t)(float, float);

typedef struct {
    TypeInfo type;
    // Alphabetized:
    float NaN, _2_sqrt_pi, e, half_pi, inf, inverse_half_pi, inverse_pi, ln10, ln2,
          log2e, pi, quarter_pi, sqrt2, sqrt_half, tau;
    // Nullary functions:
    float (*random)(void);
    // Predicates:
    float_pred_t finite, isinf, isnan;
    // Unary functions:
    float_unary_fn_t abs, acos, acosh, asin, asinh, atan, atanh, cbrt, ceil, cos, cosh, erf, erfc,
                      exp, exp10, exp2, expm1, floor, j0, j1, log, log10, log1p, log2, logb,
                      nextdown, nextup, rint, round, roundeven, significand, sin, sinh, sqrt,
                      tan, tanh, tgamma, trunc, y0, y1;
    // Binary functions:
    float_binary_fn_t atan2, copysign, dist, hypot, maxmag, minmag, mod, nextafter, pow, remainder;
    // Odds and ends:
    CORD (*format)(float f, int64_t precision);
    CORD (*scientific)(float f, int64_t precision);
} Num32_namespace_t;
extern Num32_namespace_t Num32;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
