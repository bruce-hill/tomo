#pragma once

// Type infos and methods for Nums (floating point)

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#define OptionalNum_t double
#define OptionalNum32_t float
#define N32(n) ((float)(n))
#define N64(n) ((double)(n))

Text_t Num$as_text(const void *f, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *type);
CONSTFUNC bool Num$near(double a, double b, double ratio, double absolute);
Text_t Num$format(double f, Int_t precision);
Text_t Num$scientific(double f, Int_t precision);
double Num$mod(double num, double modulus);
double Num$mod1(double num, double modulus);
CONSTFUNC bool Num$isinf(double n);
CONSTFUNC bool Num$finite(double n);
CONSTFUNC bool Num$isnan(double n);
double Num$nan(Text_t tag);
CONSTFUNC double Num$mix(double amount, double x, double y);
OptionalNum_t Num$parse(Text_t text);
MACROLIKE CONSTFUNC double Num$clamped(double x, double low, double high) {
    return (x <= low) ? low : (x >= high ? high : x);
}
MACROLIKE CONSTFUNC double Num$from_num32(Num32_t n) { return (double)n; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
MACROLIKE CONSTFUNC double Num$from_int(Int_t i, bool truncate) {
    if likely (i.small & 0x1) {
        double ret = (double)(i.small >> 2);
        if unlikely (!truncate && (int64_t)ret != (i.small >> 2))
            fail("Could not convert integer to 64-bit floating point without losing precision: %ld", i.small >> 2);
        return ret;
    } else {
        double ret = mpz_get_d(*i.big);
        if (!truncate) {
            mpz_t roundtrip;
            mpz_init_set_d(roundtrip, ret);
            if unlikely (mpz_cmp(*i.big, roundtrip) != 0)
                fail("Could not convert integer to 64-bit floating point without losing precision: %k", (Text_t[1]){Int$value_as_text(i)});
        }
        return ret;
    }
}
#pragma GCC diagnostic pop
MACROLIKE CONSTFUNC double Num$from_int64(Int64_t i, bool truncate) {
    double n = (double)i;
    if unlikely (!truncate && (Int64_t)n != i)
        fail("Could not convert integer to 64-bit floating point without losing precision: %ld", i);
    return n;
}
MACROLIKE CONSTFUNC double Num$from_int32(Int32_t i) { return (double)i; }
MACROLIKE CONSTFUNC double Num$from_int16(Int16_t i) { return (double)i; }
MACROLIKE CONSTFUNC double Num$from_int8(Int8_t i) { return (double)i; }
MACROLIKE CONSTFUNC double Num$from_byte(Byte_t i) { return (double)i; }

extern const TypeInfo_t Num$info;

Text_t Num32$as_text(const void *f, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Num32$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Num32$equal(const void *x, const void *y, const TypeInfo_t *type);
CONSTFUNC bool Num32$near(float a, float b, float ratio, float absolute);
Text_t Num32$format(float f, Int_t precision);
Text_t Num32$scientific(float f, Int_t precision);
float Num32$mod(float num, float modulus);
float Num32$mod1(float num, float modulus);
CONSTFUNC bool Num32$isinf(float n);
CONSTFUNC bool Num32$finite(float n);
CONSTFUNC bool Num32$isnan(float n);
CONSTFUNC float Num32$mix(float amount, float x, float y);
OptionalNum32_t Num32$parse(Text_t text);
float Num32$nan(Text_t tag);
MACROLIKE CONSTFUNC float Num32$clamped(float x, float low, float high) {
    return (x <= low) ? low : (x >= high ? high : x);
}
MACROLIKE CONSTFUNC float Num32$from_num(Num_t n) { return (float)n; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
MACROLIKE CONSTFUNC float Num32$from_int(Int_t i, bool truncate) {
    if likely (i.small & 0x1) {
        float ret = (float)(i.small >> 2);
        if unlikely (!truncate && (int64_t)ret != (i.small >> 2))
            fail("Could not convert integer to 32-bit floating point without losing precision: %ld", i.small >> 2);
        return ret;
    } else {
        float ret = (float)mpz_get_d(*i.big);
        if (!truncate) {
            mpz_t roundtrip;
            mpz_init_set_d(roundtrip, ret);
            if unlikely (mpz_cmp(*i.big, roundtrip) != 0)
                fail("Could not convert integer to 32-bit floating point without losing precision: %k", (Text_t[1]){Int$value_as_text(i)});
        }
        return ret;
    }
}
#pragma GCC diagnostic pop
MACROLIKE CONSTFUNC float Num32$from_int64(Int64_t i, bool truncate) {
    float n = (float)i;
    if unlikely (!truncate && (Int64_t)n != i)
        fail("Could not convert integer to 32-bit floating point without losing precision: %ld", i);
    return n;
}
MACROLIKE CONSTFUNC float Num32$from_int32(Int32_t i, bool truncate) {
    float n = (float)i;
    if unlikely (!truncate && (Int32_t)n != i)
        fail("Could not convert integer to 32-bit floating point without losing precision: %d", i);
    return n;
}
MACROLIKE CONSTFUNC float Num32$from_int16(Int16_t i) { return (float)i; }
MACROLIKE CONSTFUNC float Num32$from_int8(Int8_t i) { return (float)i; }
MACROLIKE CONSTFUNC float Num32$from_byte(Byte_t i) { return (float)i; }

extern const TypeInfo_t Num32$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
