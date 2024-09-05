#pragma once

// Integer type infos and methods

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <gmp.h>

#include "datatypes.h"
#include "functions.h"
#include "nums.h"
#include "types.h"
#include "util.h"

#define Int64_t int64_t
#define Int32_t int32_t
#define Int16_t int16_t
#define Int8_t int8_t
#define I64(x) ((int64_t)x)
#define I32(x) ((int32_t)x)
#define I16(x) ((int16_t)x)
#define I8(x) ((int8_t)x)

#define DEFINE_INT_TYPE(c_type, type_name) \
    Text_t type_name ## $as_text(const c_type *i, bool colorize, const TypeInfo *type); \
    int32_t type_name ## $compare(const c_type *x, const c_type *y, const TypeInfo *type); \
    bool type_name ## $equal(const c_type *x, const c_type *y, const TypeInfo *type); \
    Text_t type_name ## $format(c_type i, Int_t digits); \
    Text_t type_name ## $hex(c_type i, Int_t digits, bool uppercase, bool prefix); \
    Text_t type_name ## $octal(c_type i, Int_t digits, bool prefix); \
    array_t type_name ## $bits(c_type x); \
    c_type type_name ## $random(c_type min, c_type max); \
    Range_t type_name ## $to(c_type from, c_type to); \
    c_type type_name ## $from_text(Text_t text, bool *success); \
    static inline c_type type_name ## $clamped(c_type x, c_type min, c_type max) { \
        return x < min ? min : (x > max ? max : x); \
    } \
    extern const c_type type_name ## $min, type_name##$max; \
    extern const TypeInfo $ ## type_name; \
    static inline c_type type_name ## $divided_by(c_type D, c_type d) { \
        c_type q = D/d, r = D%d; \
        if (r < 0) { \
            if (d > 0) q = q-1; \
            else q = q+1; \
        } \
        return q; \
    } \
    static inline c_type type_name ## $modulo(c_type D, c_type d) { \
        c_type r = D%d; \
        if (r < 0) { \
            if (d > 0) r = r + d; \
            else r = r - d; \
        } \
        return r; \
    } \
    static inline c_type type_name ## $modulo1(c_type D, c_type d) { \
        return type_name ## $modulo(D-1, d) + 1; \
    }

DEFINE_INT_TYPE(int64_t, Int64);
DEFINE_INT_TYPE(int32_t, Int32);
DEFINE_INT_TYPE(int16_t, Int16);
DEFINE_INT_TYPE(int8_t,  Int8);
#undef DEFINE_INT_TYPE

#define Int64$abs(...) I64(labs(__VA_ARGS__))
#define Int32$abs(...) I32(abs(__VA_ARGS__))
#define Int16$abs(...) I16(abs(__VA_ARGS__))
#define Int8$abs(...) I8(abs(__VA_ARGS__))

Text_t Int$as_text(const Int_t *i, bool colorize, const TypeInfo *type);
uint64_t Int$hash(const Int_t *x, const TypeInfo *type);
int32_t Int$compare(const Int_t *x, const Int_t *y, const TypeInfo *type);
int32_t Int$compare_value(const Int_t x, const Int_t y);
bool Int$equal(const Int_t *x, const Int_t *y, const TypeInfo *type);
bool Int$equal_value(const Int_t x, const Int_t y);
Text_t Int$format(Int_t i, Int_t digits);
Text_t Int$hex(Int_t i, Int_t digits, bool uppercase, bool prefix);
Text_t Int$octal(Int_t i, Int_t digits, bool prefix);
void Int$init_random(long seed);
Int_t Int$random(Int_t min, Int_t max);
Range_t Int$to(Int_t from, Int_t to);
Int_t Int$from_str(const char *str, bool *success);
Int_t Int$from_text(Text_t text, bool *success);
Int_t Int$abs(Int_t x);
Int_t Int$power(Int_t base, Int_t exponent);
Int_t Int$sqrt(Int_t i);

#define BIGGEST_SMALL_INT ((1<<29)-1)

#define Int$from_mpz(mpz) (\
    mpz_cmpabs_ui(mpz, BIGGEST_SMALL_INT) <= 0 ? ({ \
        (Int_t){.small=(mpz_get_si(mpz)<<2)|1}; \
    }) : ({ \
        mpz_t *result_obj = new(mpz_t); \
        memcpy(result_obj, &mpz, sizeof(mpz_t)); \
        (Int_t){.big=result_obj}; \
    }))

#define mpz_init_set_int(mpz, i) do { \
    if (__builtin_expect((i).small & 1, 1)) mpz_init_set_si(mpz, (i).small >> 2); \
    else mpz_init_set(mpz, *(i).big); \
} while (0)

#define I(i) ((int64_t)(i) == (int32_t)(i) ? ((Int_t){.small=((uint64_t)(i)<<2)|1}) : Int64_to_Int(i))
#define I_small(i) ((Int_t){.small=((uint64_t)(i)<<2)|1})
#define I_is_zero(i) ((i).small == 1)

Int_t Int$slow_plus(Int_t x, Int_t y);
Int_t Int$slow_minus(Int_t x, Int_t y);
Int_t Int$slow_times(Int_t x, Int_t y);
Int_t Int$slow_divided_by(Int_t x, Int_t y);
Int_t Int$slow_modulo(Int_t x, Int_t y);
Int_t Int$slow_modulo1(Int_t x, Int_t y);
Int_t Int$slow_left_shifted(Int_t x, Int_t y);
Int_t Int$slow_right_shifted(Int_t x, Int_t y);
Int_t Int$slow_bit_and(Int_t x, Int_t y);
Int_t Int$slow_bit_or(Int_t x, Int_t y);
Int_t Int$slow_bit_xor(Int_t x, Int_t y);
Int_t Int$slow_negative(Int_t x);
Int_t Int$slow_negated(Int_t x);
Int_t Int$abs(Int_t x);
bool Int$is_prime(Int_t x, Int_t reps);
Int_t Int$next_prime(Int_t x);
Int_t Int$prev_prime(Int_t x);

extern const TypeInfo $Int;

static inline Int_t Int$clamped(Int_t x, Int_t low, Int_t high)
{
    return (Int$compare(&x, &low, &$Int) <= 0) ? low : (Int$compare(&x, &high, &$Int) >= 0 ? high : x);
}

// Fast-path inline versions for the common case where integer arithmetic is
// between two small ints.

static inline Int_t Int$plus(Int_t x, Int_t y) {
    const int64_t z = (int64_t)((uint64_t)x.small + (uint64_t)y.small);
    if (__builtin_expect(((z|2) == (int32_t)z), 1))
        return (Int_t){.small=(z-1)};
    return Int$slow_plus(x, y);
}

static inline Int_t Int$minus(Int_t x, Int_t y) {
    const int64_t z = (int64_t)(((uint64_t)x.small ^ 3) - (uint64_t)y.small);
    if (__builtin_expect(((z & ~2) == (int32_t)z), 1))
        return (Int_t){.small=z};
    return Int$slow_minus(x, y);
}

static inline Int_t Int$times(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        const int64_t z = (x.small>>1) * (y.small>>1);
        if (__builtin_expect(z == (int32_t)z, 1))
            return (Int_t){.small=z+1};
    }
    return Int$slow_times(x, y);
}

static inline Int_t Int$divided_by(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small>>2);
        const int64_t d = (y.small>>2);
        int64_t q = D/d;
        int64_t r = D%d;
        if (r < 0) {
            if (d > 0) q = q-1;
            else q = q+1;
        }
        if (__builtin_expect(q == (int32_t)q, 1))
            return (Int_t){.small=(q<<2)|1};
    }
    return Int$slow_divided_by(x, y);
}

static inline Int_t Int$modulo(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        // Euclidean modulus, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small>>2);
        const int64_t d = (y.small>>2);
        int64_t r = D%d;
        if (r < 0) {
            if (d > 0) r = r + d;
            else r = r - d;
        }
        return (Int_t){.small=(r<<2)|1};
    }
    return Int$slow_modulo(x, y);
}

static inline Int_t Int$modulo1(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        // Euclidean modulus, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small>>2)-1;
        const int64_t d = (y.small>>2);
        int64_t r = D%d;
        if (r < 0) {
            if (d > 0) r = r + d;
            else r = r - d;
        }
        return (Int_t){.small=((r+1)<<2)|1};
    }
    return Int$slow_modulo1(x, y);
}

static inline Int_t Int$left_shifted(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        const int64_t z = ((x.small>>2) << (y.small>>2))<<2;
        if (__builtin_expect(z == (int32_t)z, 1))
            return (Int_t){.small=z+1};
    }
    return Int$slow_left_shifted(x, y);
}

static inline Int_t Int$right_shifted(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        const int64_t z = ((x.small>>2) >> (y.small>>2))<<2;
        if (__builtin_expect(z == (int32_t)z, 1))
            return (Int_t){.small=z+1};
    }
    return Int$slow_right_shifted(x, y);
}

static inline Int_t Int$bit_and(Int_t x, Int_t y) {
    const int64_t z = x.small & y.small;
    if (__builtin_expect((z & 1) == 1, 1))
        return (Int_t){.small=z};
    return Int$slow_bit_and(x, y);
}

static inline Int_t Int$bit_or(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) == 1, 1))
        return (Int_t){.small=(x.small | y.small)};
    return Int$slow_bit_or(x, y);
}

static inline Int_t Int$bit_xor(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) == 1, 1))
        return (Int_t){.small=(x.small ^ y.small) | 1};
    return Int$slow_bit_xor(x, y);
}

static inline Int_t Int$negated(Int_t x)
{
    if (__builtin_expect((x.small & 1), 1))
        return (Int_t){.small=(~x.small) ^ 3};
    return Int$slow_negated(x);
}

static inline Int_t Int$negative(Int_t x)
{
    if (__builtin_expect((x.small & 1), 1))
        return (Int_t){.small=((-((x.small)>>2))<<2) | 1};
    return Int$slow_negative(x);
}

static inline bool Int$is_negative(Int_t x)
{
    if (__builtin_expect((x.small & 1), 1))
        return x.small < 0;
    return Int$compare_value(x, I_small(0)) < 0;
}

// Conversion functions:

static inline Int_t Int64_to_Int(int64_t i)
{
    int64_t z = i<<2;
    if (__builtin_expect(z == (int32_t)z, 1))
        return (Int_t){.small=z+1};
    mpz_t result;
    mpz_init_set_si(result, i);
    return Int$from_mpz(result);
}

#define Int32_to_Int(i) Int64_to_Int(i)
#define Int16_to_Int(i) Int64_to_Int(i)
#define Int8_to_Int(i) Int64_to_Int(i)

static inline Int64_t Int_to_Int64(Int_t i, bool truncate) {
    if (__builtin_expect(i.small & 1, 1))
        return (int64_t)(i.small >> 2);
    if (__builtin_expect(!truncate && !mpz_fits_slong_p(*i.big), 0))
        fail("Integer is too big to fit in a 64-bit integer!");
    return mpz_get_si(*i.big);
}

static inline Int32_t Int_to_Int32(Int_t i, bool truncate) {
    int64_t i64 = Int_to_Int64(i, truncate);
    int32_t i32 = (int32_t)i64;
    if (__builtin_expect(i64 != i32 && !truncate, 0))
        fail("Integer is too big to fit in a 32-bit integer!");
    return i32;
}

static inline Int16_t Int_to_Int16(Int_t i, bool truncate) {
    int64_t i64 = Int_to_Int64(i, truncate);
    int16_t i16 = (int16_t)i64;
    if (__builtin_expect(i64 != i16 && !truncate, 0))
        fail("Integer is too big to fit in a 16-bit integer!");
    return i16;
}

static inline Int8_t Int_to_Int8(Int_t i, bool truncate) {
    int64_t i64 = Int_to_Int64(i, truncate);
    int8_t i8 = (int8_t)i64;
    if (__builtin_expect(i64 != i8 && !truncate, 0))
        fail("Integer is too big to fit in an 8-bit integer!");
    return i8;
}

static inline Int_t Num_to_Int(double n)
{
    mpz_t result;
    mpz_init_set_d(result, n);
    return Int$from_mpz(result);
}

static inline double Int_to_Num(Int_t i)
{
    if (__builtin_expect(i.small & 1, 1))
        return (double)(i.small >> 2);

    return mpz_get_d(*i.big);
}

#define Int_to_Num32(i) (Num32_t)Int_to_Num(i)

#define CONVERSION_FUNC(hi, lo) \
    static inline int##lo##_t Int##hi##_to_Int##lo(int##hi##_t i, bool truncate) { \
        if (__builtin_expect(!truncate && (i != (int##lo##_t)i), 0)) \
            fail("Cannot truncate the Int" #hi " %ld to an Int" #lo, (int64_t)i); \
        return (int##lo##_t)i; \
    }

CONVERSION_FUNC(64, 32)
CONVERSION_FUNC(64, 16)
CONVERSION_FUNC(64, 8)
CONVERSION_FUNC(32, 16)
CONVERSION_FUNC(32, 8)
CONVERSION_FUNC(16, 8)
#undef CONVERSION_FUNC

#define CONVERSION_FUNC(num, int_type) \
    static inline int_type##_t num##_to_##int_type(num##_t n, bool truncate) { \
        num##_t rounded = round(n); \
        if (__builtin_expect(!truncate && (num##_t)(int_type##_t)rounded != rounded, 0)) \
            fail("Cannot truncate the " #num " %g to an " #int_type, rounded); \
        return (int_type##_t)rounded; \
    }

CONVERSION_FUNC(Num, Int64)
CONVERSION_FUNC(Num, Int32)
CONVERSION_FUNC(Num, Int16)
CONVERSION_FUNC(Num, Int8)
CONVERSION_FUNC(Num32, Int64)
CONVERSION_FUNC(Num32, Int32)
CONVERSION_FUNC(Num32, Int16)
CONVERSION_FUNC(Num32, Int8)
#undef CONVERSION_FUNC

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
