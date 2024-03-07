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
double Num__random(void);
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
float Num32__random(void);
float Num32__nan(CORD tag);
extern const TypeInfo Num32;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
