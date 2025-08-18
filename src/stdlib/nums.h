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

Text_t Numヽas_text(const void *f, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Numヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Numヽequal(const void *x, const void *y, const TypeInfo_t *type);
CONSTFUNC bool Numヽnear(double a, double b, double ratio, double absolute);
Text_t Numヽpercent(double f, double precision);
double CONSTFUNC Numヽwith_precision(double num, double precision);
double Numヽmod(double num, double modulus);
double Numヽmod1(double num, double modulus);
CONSTFUNC bool Numヽisinf(double n);
CONSTFUNC bool Numヽfinite(double n);
CONSTFUNC bool Numヽisnan(double n);
double Numヽnan(Text_t tag);
CONSTFUNC double Numヽmix(double amount, double x, double y);
OptionalNum_t Numヽparse(Text_t text, Text_t *remainder);
CONSTFUNC bool Numヽis_between(const double x, const double low, const double high);
CONSTFUNC double Numヽclamped(double x, double low, double high);
MACROLIKE CONSTFUNC double Numヽfrom_num32(Num32_t n) { return (double)n; }
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
MACROLIKE CONSTFUNC double Numヽfrom_int(Int_t i, bool truncate) {
    if likely (i.small & 0x1) {
        double ret = (double)(i.small >> 2);
        if unlikely (!truncate && (int64_t)ret != (i.small >> 2))
            fail("Could not convert integer to 64-bit floating point without losing precision: ", i.small >> 2);
        return ret;
    } else {
        double ret = mpz_get_d(*i.big);
        if (!truncate) {
            mpz_t roundtrip;
            mpz_init_set_d(roundtrip, ret);
            if unlikely (mpz_cmp(*i.big, roundtrip) != 0)
                fail("Could not convert integer to 64-bit floating point without losing precision: ", i);
        }
        return ret;
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
MACROLIKE CONSTFUNC double Numヽfrom_int64(Int64_t i, bool truncate) {
    double n = (double)i;
    if unlikely (!truncate && (Int64_t)n != i)
        fail("Could not convert integer to 64-bit floating point without losing precision: ", i);
    return n;
}
MACROLIKE CONSTFUNC double Numヽfrom_int32(Int32_t i) { return (double)i; }
MACROLIKE CONSTFUNC double Numヽfrom_int16(Int16_t i) { return (double)i; }
MACROLIKE CONSTFUNC double Numヽfrom_int8(Int8_t i) { return (double)i; }
MACROLIKE CONSTFUNC double Numヽfrom_byte(Byte_t i) { return (double)i; }

extern const TypeInfo_t Numヽinfo;

Text_t Num32ヽas_text(const void *f, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Num32ヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Num32ヽequal(const void *x, const void *y, const TypeInfo_t *type);
CONSTFUNC bool Num32ヽnear(float a, float b, float ratio, float absolute);
Text_t Num32ヽpercent(float f, float precision);
float CONSTFUNC Num32ヽwith_precision(float num, float precision);
float Num32ヽmod(float num, float modulus);
float Num32ヽmod1(float num, float modulus);
CONSTFUNC bool Num32ヽisinf(float n);
CONSTFUNC bool Num32ヽfinite(float n);
CONSTFUNC bool Num32ヽisnan(float n);
CONSTFUNC float Num32ヽmix(float amount, float x, float y);
OptionalNum32_t Num32ヽparse(Text_t text, Text_t *remainder);
float Num32ヽnan(Text_t tag);
CONSTFUNC bool Num32ヽis_between(const float x, const float low, const float high);
CONSTFUNC float Num32ヽclamped(float x, float low, float high);
MACROLIKE CONSTFUNC float Num32ヽfrom_num(Num_t n) { return (float)n; }
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
MACROLIKE CONSTFUNC float Num32ヽfrom_int(Int_t i, bool truncate) {
    if likely (i.small & 0x1) {
        float ret = (float)(i.small >> 2);
        if unlikely (!truncate && (int64_t)ret != (i.small >> 2))
            fail("Could not convert integer to 32-bit floating point without losing precision: ", i.small >> 2);
        return ret;
    } else {
        float ret = (float)mpz_get_d(*i.big);
        if (!truncate) {
            mpz_t roundtrip;
            mpz_init_set_d(roundtrip, (double)ret);
            if unlikely (mpz_cmp(*i.big, roundtrip) != 0)
                fail("Could not convert integer to 32-bit floating point without losing precision: ", i);
        }
        return ret;
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
MACROLIKE CONSTFUNC float Num32ヽfrom_int64(Int64_t i, bool truncate) {
    float n = (float)i;
    if unlikely (!truncate && (Int64_t)n != i)
        fail("Could not convert integer to 32-bit floating point without losing precision: ", i);
    return n;
}
MACROLIKE CONSTFUNC float Num32ヽfrom_int32(Int32_t i, bool truncate) {
    float n = (float)i;
    if unlikely (!truncate && (Int32_t)n != i)
        fail("Could not convert integer to 32-bit floating point without losing precision: ", i);
    return n;
}
MACROLIKE CONSTFUNC float Num32ヽfrom_int16(Int16_t i) { return (float)i; }
MACROLIKE CONSTFUNC float Num32ヽfrom_int8(Int8_t i) { return (float)i; }
MACROLIKE CONSTFUNC float Num32ヽfrom_byte(Byte_t i) { return (float)i; }

extern const TypeInfo_t Num32ヽinfo;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
