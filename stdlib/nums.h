#pragma once

// Type infos and methods for Nums (floating point)

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

#define Num_t double
#define Num32_t float
#define OptionalNum_t double
#define OptionalNum32_t float
#define N32(n) ((float)n)
#define N64(n) ((double)n)

Text_t Num$as_text(const double *f, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare(const double *x, const double *y, const TypeInfo_t *type);
PUREFUNC bool Num$equal(const double *x, const double *y, const TypeInfo_t *type);
CONSTFUNC bool Num$near(double a, double b, double ratio, double absolute);
Text_t Num$format(double f, Int_t precision);
Text_t Num$scientific(double f, Int_t precision);
double Num$mod(double num, double modulus);
CONSTFUNC bool Num$isinf(double n);
CONSTFUNC bool Num$finite(double n);
CONSTFUNC bool Num$isnan(double n);
double Num$nan(Text_t tag);
CONSTFUNC double Num$mix(double amount, double x, double y);
OptionalNum_t Num$from_text(Text_t text);
MACROLIKE CONSTFUNC double Num$clamped(double x, double low, double high) {
    return (x <= low) ? low : (x >= high ? high : x);
}
extern const TypeInfo_t Num$info;

Text_t Num32$as_text(const float *f, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Num32$compare(const float *x, const float *y, const TypeInfo_t *type);
PUREFUNC bool Num32$equal(const float *x, const float *y, const TypeInfo_t *type);
CONSTFUNC bool Num32$near(float a, float b, float ratio, float absolute);
Text_t Num32$format(float f, Int_t precision);
Text_t Num32$scientific(float f, Int_t precision);
float Num32$mod(float num, float modulus);
CONSTFUNC bool Num32$isinf(float n);
CONSTFUNC bool Num32$finite(float n);
CONSTFUNC bool Num32$isnan(float n);
CONSTFUNC float Num32$mix(float amount, float x, float y);
OptionalNum32_t Num32$from_text(Text_t text);
float Num32$nan(Text_t tag);
MACROLIKE CONSTFUNC float Num32$clamped(float x, float low, float high) {
    return (x <= low) ? low : (x >= high ? high : x);
}
extern const TypeInfo_t Num32$info;

#define Num_to_Num32(n) ((Num32_t)(n))
#define Num32_to_Num(n) ((Num_t)(n))

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
