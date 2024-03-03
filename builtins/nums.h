#pragma once
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define Num_t double
#define Num32_t float

CORD Num__as_text(const double *f, bool colorize, const TypeInfo *type);
int32_t Num__compare(const double *x, const double *y, const TypeInfo *type);
bool Num__equal(const double *x, const double *y, const TypeInfo *type);
bool Num__near(double a, double b, double ratio, double absolute);
CORD Num__format(double f, int64_t precision);
CORD Num__scientific(double f, int64_t precision);
double Num__mod(double num, double modulus);
bool Num__isinf(double n);
bool Num__finite(double n);
bool Num__isnan(double n);
double Num__nan(CORD tag);
// Constants:
#define C(name) const double Num__##name = M_##name;
C(2_SQRTPI) C(E) C(PI_2) C(2_PI) C(1_PI) C(LN10) C(LN2) C(LOG2E) C(PI) C(PI_4) C(SQRT2) C(SQRT1_2)
const double Num__INF = INFINITY, Num__TAU = 2.*M_PI;
#undef C
double Num__random(void);
#define F(name) double (*Num__##name)(double n) = name;
double (*Num__abs)(double) = fabs;
F(acos) F(acosh) F(asin) F(asinh) F(atan) F(atanh) F(cbrt) F(ceil) F(cos) F(cosh) F(erf) F(erfc)
F(exp) F(exp2) F(expm1) F(floor) F(j0) F(j1) F(log) F(log10) F(log1p) F(log2) F(logb)
F(rint) F(round) F(significand) F(sin) F(sinh) F(sqrt)
F(tan) F(tanh) F(tgamma) F(trunc) F(y0) F(y1)
#undef F
#define F(name) double (*Num__##name)(double x, double y) = name;
F(atan2) F(copysign) F(fdim) F(hypot) F(nextafter) F(pow) F(remainder)
#undef F
extern const TypeInfo Num;

CORD Num32__as_text(const float *f, bool colorize, const TypeInfo *type);
int32_t Num32__compare(const float *x, const float *y, const TypeInfo *type);
bool Num32__equal(const float *x, const float *y, const TypeInfo *type);
bool Num32__near(float a, float b, float ratio, float absolute);
CORD Num32__format(float f, int64_t precision);
CORD Num32__scientific(float f, int64_t precision);
float Num32__mod(float num, float modulus);
bool Num32__isinf(float n);
bool Num32__finite(float n);
bool Num32__isnan(float n);
// Constants:
#define C(name) const float Num32__##name = M_##name;
C(2_SQRTPI) C(E) C(PI_2) C(2_PI) C(1_PI) C(LN10) C(LN2) C(LOG2E) C(PI) C(PI_4) C(SQRT2) C(SQRT1_2)
const float Num32__INF = INFINITY, Num32__TAU = 2.*M_PI;
#undef C
float Num32__random(void);
float Num32__nan(CORD tag);
#define F(name) float (*Num32__##name)(float n) = name##f;
float (*Num32__abs)(float) = fabsf;
F(acos) F(acosh) F(asin) F(asinh) F(atan) F(atanh) F(cbrt) F(ceil) F(cos) F(cosh) F(erf) F(erfc)
F(exp) F(exp2) F(expm1) F(floor) F(j0) F(j1) F(log) F(log10) F(log1p) F(log2) F(logb)
F(rint) F(round) F(significand) F(sin) F(sinh) F(sqrt)
F(tan) F(tanh) F(tgamma) F(trunc) F(y0) F(y1)
#undef F
#define F(name) float (*Num32__##name)(float x, float y) = name##f;
F(atan2) F(copysign) F(fdim) F(hypot) F(nextafter) F(pow) F(remainder)
#undef F
extern const TypeInfo Num32;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
