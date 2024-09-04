#pragma once

// Type infos and methods for Nums (floating point)

#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define Num_t double
#define Num32_t float
#define N32(n) ((float)n)
#define N64(n) ((double)n)

Text_t Num$as_text(const double *f, bool colorize, const TypeInfo *type);
int32_t Num$compare(const double *x, const double *y, const TypeInfo *type);
bool Num$equal(const double *x, const double *y, const TypeInfo *type);
bool Num$near(double a, double b, double ratio, double absolute);
Text_t Num$format(double f, Int_t precision);
Text_t Num$scientific(double f, Int_t precision);
double Num$mod(double num, double modulus);
bool Num$isinf(double n);
bool Num$finite(double n);
bool Num$isnan(double n);
double Num$nan(Text_t tag);
double Num$random(void);
double Num$mix(double amount, double x, double y);
double Num$from_text(Text_t text, bool *success);
static inline double Num$clamped(double x, double low, double high) {
    return (x <= low) ? low : (x >= high ? high : x);
}
extern const TypeInfo $Num;

Text_t Num32$as_text(const float *f, bool colorize, const TypeInfo *type);
int32_t Num32$compare(const float *x, const float *y, const TypeInfo *type);
bool Num32$equal(const float *x, const float *y, const TypeInfo *type);
bool Num32$near(float a, float b, float ratio, float absolute);
Text_t Num32$format(float f, Int_t precision);
Text_t Num32$scientific(float f, Int_t precision);
float Num32$mod(float num, float modulus);
bool Num32$isinf(float n);
bool Num32$finite(float n);
bool Num32$isnan(float n);
float Num32$random(void);
float Num32$mix(float amount, float x, float y);
float Num32$from_text(Text_t text, bool *success);
float Num32$nan(Text_t tag);
static inline float Num32$clamped(float x, float low, float high) {
    return (x <= low) ? low : (x >= high ? high : x);
}
extern const TypeInfo $Num32;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
