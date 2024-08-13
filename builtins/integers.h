#pragma once

// Integer type infos and methods

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <gmp.h>

#include "datatypes.h"
#include "types.h"

#define Int64_t int64_t
#define Int32_t int32_t
#define Int16_t int16_t
#define Int8_t int8_t
#define I64(x) ((int64_t)x)
#define I32(x) ((int32_t)x)
#define I16(x) ((int16_t)x)
#define I8(x) ((int8_t)x)

#define DEFINE_INT_TYPE(c_type, type_name) \
    CORD type_name ## $as_text(const c_type *i, bool colorize, const TypeInfo *type); \
    int32_t type_name ## $compare(const c_type *x, const c_type *y, const TypeInfo *type); \
    bool type_name ## $equal(const c_type *x, const c_type *y, const TypeInfo *type); \
    CORD type_name ## $format(c_type i, Int_t digits); \
    CORD type_name ## $hex(c_type i, Int_t digits, bool uppercase, bool prefix); \
    CORD type_name ## $octal(c_type i, Int_t digits, bool prefix); \
    array_t type_name ## $bits(c_type x); \
    c_type type_name ## $random(c_type min, c_type max); \
    Range_t type_name ## $to(c_type from, c_type to); \
    c_type type_name ## $from_text(CORD text, CORD *the_rest); \
    extern const c_type type_name ## $min, type_name##$max; \
    extern const TypeInfo $ ## type_name;

DEFINE_INT_TYPE(int64_t, Int64);
DEFINE_INT_TYPE(int32_t, Int32);
DEFINE_INT_TYPE(int16_t, Int16);
DEFINE_INT_TYPE(int8_t,  Int8);
#undef DEFINE_INT_TYPE

#define Int64$abs(...) I64(labs(__VA_ARGS__))
#define Int32$abs(...) I32(abs(__VA_ARGS__))
#define Int16$abs(...) I16(abs(__VA_ARGS__))
#define Int8$abs(...) I8(abs(__VA_ARGS__))

CORD Int$as_text(const Int_t *i, bool colorize, const TypeInfo *type);
uint32_t Int$hash(const Int_t *x, const TypeInfo *type);
int32_t Int$compare(const Int_t *x, const Int_t *y, const TypeInfo *type);
int32_t Int$compare_value(const Int_t x, const Int_t y);
bool Int$equal(const Int_t *x, const Int_t *y, const TypeInfo *type);
bool Int$equal_value(const Int_t x, const Int_t y);
CORD Int$format(Int_t i, Int_t digits);
CORD Int$hex(Int_t i, Int_t digits, bool uppercase, bool prefix);
CORD Int$octal(Int_t i, Int_t digits, bool prefix);
void Int$init_random(long seed);
Int_t Int$random(Int_t min, Int_t max);
Range_t Int$to(Int_t from, Int_t to);
Int_t Int$from_text(CORD text);
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

#define Int$as_i64(i) (__builtin_expect((i).small & 1, 1) ? (int64_t)((i).small >> 2) : \
                       ({ if (!__builtin_expect(mpz_fits_slong_p(*(i).big), 1)) fail("Integer is too big to fit in a 64-bit integer!"); \
                        mpz_get_si(*(i).big); }))
Int_t Int$from_i64(int64_t i);
Int_t Int$from_num(double n);
double Int$as_num(Int_t i);
#define I(i) ((int64_t)(i) == (int32_t)(i) ? ((Int_t){.small=((uint64_t)(i)<<2)|1}) : Int$from_i64(i))

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

extern const TypeInfo $Int;

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
        const int64_t z = 4*(x.small>>1) / (y.small>>1);
        if (__builtin_expect(z == (int32_t)z, 1))
            return (Int_t){.small=z+1};
    }
    return Int$slow_divided_by(x, y);
}

static inline Int_t Int$modulo(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        int64_t mod = (x.small>>2) % (y.small>>2);
        if (mod < 0) mod += (y.small>>2);
        return (Int_t){.small=(mod<<2)+1};
    }
    return Int$slow_modulo(x, y);
}

static inline Int_t Int$modulo1(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        int64_t mod = ((x.small>>2)-1) % (y.small>>2);
        if (mod < 0) mod += (y.small>>2);
        return (Int_t){.small=((mod+1)<<2)+1};
    }
    return Int$slow_modulo1(x, y);
}

static inline Int_t Int$left_shifted(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        const int64_t z = 4*((x.small>>2) << (y.small>>2));
        if (__builtin_expect(z == (int32_t)z, 1))
            return (Int_t){.small=z+1};
    }
    return Int$slow_left_shifted(x, y);
}

static inline Int_t Int$right_shifted(Int_t x, Int_t y) {
    if (__builtin_expect(((x.small & y.small) & 1) != 0, 1)) {
        const int64_t z = 4*((x.small>>2) >> (y.small>>2));
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
        return (Int_t){.small=4*-((x.small)>>2) + 1};
    return Int$slow_negative(x);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
