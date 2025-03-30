#pragma once

// Integer type infos and methods

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <gmp.h>

#include "print.h"
#include "datatypes.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#define I64(x) ((int64_t)x)
#define I32(x) ((int32_t)x)
#define I16(x) ((int16_t)x)
#define I8(x) ((int8_t)x)

#define DEFINE_INT_TYPE(c_type, type_name) \
    typedef struct { \
        c_type value; \
        bool is_none:1; \
    } Optional ## type_name ## _t; \
    Text_t type_name ## $as_text(const void *i, bool colorize, const TypeInfo_t *type); \
    PUREFUNC int32_t type_name ## $compare(const void *x, const void *y, const TypeInfo_t *type); \
    PUREFUNC bool type_name ## $equal(const void *x, const void *y, const TypeInfo_t *type); \
    Text_t type_name ## $format(c_type i, Int_t digits); \
    Text_t type_name ## $hex(c_type i, Int_t digits, bool uppercase, bool prefix); \
    Text_t type_name ## $octal(c_type i, Int_t digits, bool prefix); \
    Array_t type_name ## $bits(c_type x); \
    Closure_t type_name ## $to(c_type first, c_type last, Optional ## type_name ## _t step); \
    Closure_t type_name ## $onward(c_type first, c_type step); \
    PUREFUNC Optional ## type_name ## _t type_name ## $parse(Text_t text); \
    MACROLIKE PUREFUNC c_type type_name ## $clamped(c_type x, c_type min, c_type max) { \
        return x < min ? min : (x > max ? max : x); \
    } \
    MACROLIKE CONSTFUNC c_type type_name ## $from_byte(Byte_t b) { return (c_type)b; } \
    MACROLIKE CONSTFUNC c_type type_name ## $from_bool(Bool_t b) { return (c_type)b; } \
    CONSTFUNC c_type type_name ## $gcd(c_type x, c_type y); \
    extern const c_type type_name ## $min, type_name##$max; \
    extern const TypeInfo_t type_name ## $info; \
    MACROLIKE c_type type_name ## $divided_by(c_type D, c_type d) { \
        c_type q = D/d, r = D%d; \
        q -= (r < 0) * (2*(d > 0) - 1); \
        return q; \
    } \
    MACROLIKE c_type type_name ## $modulo(c_type D, c_type d) { \
        c_type r = D%d; \
        r -= (r < 0) * (2*(d < 0) - 1) * d; \
        return r; \
    } \
    MACROLIKE c_type type_name ## $modulo1(c_type D, c_type d) { \
        return type_name ## $modulo(D-1, d) + 1; \
    } \
    MACROLIKE PUREFUNC c_type type_name ## $wrapping_plus(c_type x, c_type y) { \
        return (c_type)((u##c_type)x + (u##c_type)y); \
    } \
    MACROLIKE PUREFUNC c_type type_name ## $wrapping_minus(c_type x, c_type y) { \
        return (c_type)((u##c_type)x + (u##c_type)y); \
    } \
    MACROLIKE PUREFUNC c_type type_name ## $unsigned_left_shifted(c_type x, c_type y) { \
        return (c_type)((u##c_type)x << y); \
    } \
    MACROLIKE PUREFUNC c_type type_name ## $unsigned_right_shifted(c_type x, c_type y) { \
        return (c_type)((u##c_type)x >> y); \
    }

DEFINE_INT_TYPE(int64_t, Int64)
DEFINE_INT_TYPE(int32_t, Int32)
DEFINE_INT_TYPE(int16_t, Int16)
DEFINE_INT_TYPE(int8_t,  Int8)
#undef DEFINE_INT_TYPE

#define NONE_INT64 ((OptionalInt64_t){.is_none=true})
#define NONE_INT32 ((OptionalInt32_t){.is_none=true})
#define NONE_INT16 ((OptionalInt16_t){.is_none=true})
#define NONE_INT8 ((OptionalInt8_t){.is_none=true})

#define Int64$abs(...) I64(labs(__VA_ARGS__))
#define Int32$abs(...) I32(abs(__VA_ARGS__))
#define Int16$abs(...) I16(abs(__VA_ARGS__))
#define Int8$abs(...) I8(abs(__VA_ARGS__))

void Int64$serialize(const void *obj, FILE *out, Table_t*, const TypeInfo_t*);
void Int64$deserialize(FILE *in, void *outval, Array_t*, const TypeInfo_t*);
void Int32$serialize(const void *obj, FILE *out, Table_t*, const TypeInfo_t*);
void Int32$deserialize(FILE *in, void *outval, Array_t*, const TypeInfo_t*);

int Int$print(FILE *f, Int_t i);
Text_t Int$as_text(const void *i, bool colorize, const TypeInfo_t *type);
Text_t Int$value_as_text(Int_t i);
PUREFUNC uint64_t Int$hash(const void *x, const TypeInfo_t *type);
PUREFUNC int32_t Int$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC int32_t Int$compare_value(const Int_t x, const Int_t y);
PUREFUNC bool Int$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Int$equal_value(const Int_t x, const Int_t y);
Text_t Int$format(Int_t i, Int_t digits);
Text_t Int$hex(Int_t i, Int_t digits, bool uppercase, bool prefix);
Text_t Int$octal(Int_t i, Int_t digits, bool prefix);
PUREFUNC Closure_t Int$to(Int_t first, Int_t last, OptionalInt_t step);
PUREFUNC Closure_t Int$onward(Int_t first, Int_t step);
OptionalInt_t Int$from_str(const char *str);
OptionalInt_t Int$parse(Text_t text);
Int_t Int$abs(Int_t x);
Int_t Int$power(Int_t base, Int_t exponent);
Int_t Int$gcd(Int_t x, Int_t y);
OptionalInt_t Int$sqrt(Int_t i);

#define BIGGEST_SMALL_INT 0x3fffffff
#define SMALLEST_SMALL_INT -0x40000000

#define Int$from_mpz(mpz) (\
    mpz_cmpabs_ui(mpz, BIGGEST_SMALL_INT) <= 0 ? ( \
        (Int_t){.small=(mpz_get_si(mpz)<<2L)|1L} \
    ) : ( \
        (Int_t){.big=memcpy(new(mpz_t), &mpz, sizeof(mpz_t))} \
    ))

#define mpz_init_set_int(mpz, i) do { \
    if likely ((i).small & 1L) mpz_init_set_si(mpz, (i).small >> 2L); \
    else mpz_init_set(mpz, *(i).big); \
} while (0)

#define I_small(i) ((Int_t){.small=(int64_t)((uint64_t)(i)<<2L)|1L})
#define I(i) _Generic(i, int8_t: I_small(i), int16_t: I_small(i), default: Int$from_int64(i))
#define I_is_zero(i) ((i).small == 1L)

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
bool Int$is_prime(Int_t x, Int_t reps);
Int_t Int$next_prime(Int_t x);
#if __GNU_MP_VERSION >= 6
#if __GNU_MP_VERSION_MINOR >= 3
Int_t Int$prev_prime(Int_t x);
#endif
#endif
Int_t Int$choose(Int_t n, Int_t k);
Int_t Int$factorial(Int_t n);

extern const TypeInfo_t Int$info;

MACROLIKE PUREFUNC Int_t Int$clamped(Int_t x, Int_t low, Int_t high) {
    return (Int$compare(&x, &low, &Int$info) <= 0) ? low : (Int$compare(&x, &high, &Int$info) >= 0 ? high : x);
}

// Fast-path inline versions for the common case where integer arithmetic is
// between two small ints.

MACROLIKE Int_t Int$plus(Int_t x, Int_t y) {
    const int64_t z = (int64_t)((uint64_t)x.small + (uint64_t)y.small);
    if likely ((z|2L) == (int32_t)z)
        return (Int_t){.small=(z-1L)};
    return Int$slow_plus(x, y);
}

MACROLIKE Int_t Int$minus(Int_t x, Int_t y) {
    const int64_t z = (int64_t)(((uint64_t)x.small ^ 3L) - (uint64_t)y.small);
    if likely ((z & ~2L) == (int32_t)z)
        return (Int_t){.small=z};
    return Int$slow_minus(x, y);
}

MACROLIKE Int_t Int$times(Int_t x, Int_t y) {
    if likely ((x.small & y.small) & 1L) {
        const int64_t z = (x.small>>1L) * (y.small>>1L);
        if likely (z == (int32_t)z)
            return (Int_t){.small=z+1L};
    }
    return Int$slow_times(x, y);
}

MACROLIKE Int_t Int$divided_by(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small>>2L);
        const int64_t d = (y.small>>2L);
        int64_t q = D/d, r = D%d;
        q -= (r < 0L) * (2L*(d > 0L) - 1L);
        if likely (q == (int32_t)q)
            return (Int_t){.small=(q<<2L)|1L};
    }
    return Int$slow_divided_by(x, y);
}

MACROLIKE Int_t Int$modulo(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        // Euclidean modulus, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small>>2L);
        const int64_t d = (y.small>>2L);
        int64_t r = D%d;
        r -= (r < 0L) * (2L*(d < 0L) - 1L) * d;
        return (Int_t){.small=(r<<2L)|1L};
    }
    return Int$slow_modulo(x, y);
}

MACROLIKE Int_t Int$modulo1(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        // Euclidean modulus, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small>>2L)-1L;
        const int64_t d = (y.small>>2L);
        int64_t r = D%d;
        r -= (r < 0L) * (2L*(d < 0L) - 1L) * d;
        return (Int_t){.small=((r+1L)<<2L)|1L};
    }
    return Int$slow_modulo1(x, y);
}

MACROLIKE Int_t Int$left_shifted(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        const int64_t z = ((x.small>>2L) << (y.small>>2L))<<2L;
        if likely (z == (int32_t)z)
            return (Int_t){.small=z+1L};
    }
    return Int$slow_left_shifted(x, y);
}

MACROLIKE Int_t Int$right_shifted(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        const int64_t z = ((x.small>>2L) >> (y.small>>2L))<<2L;
        if likely (z == (int32_t)z)
            return (Int_t){.small=z+1L};
    }
    return Int$slow_right_shifted(x, y);
}

MACROLIKE Int_t Int$bit_and(Int_t x, Int_t y) {
    const int64_t z = x.small & y.small;
    if likely (z & 1L)
        return (Int_t){.small=z};
    return Int$slow_bit_and(x, y);
}

MACROLIKE Int_t Int$bit_or(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L)
        return (Int_t){.small=(x.small | y.small)};
    return Int$slow_bit_or(x, y);
}

MACROLIKE Int_t Int$bit_xor(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L)
        return (Int_t){.small=(x.small ^ y.small) | 1L};
    return Int$slow_bit_xor(x, y);
}

MACROLIKE Int_t Int$negated(Int_t x) {
    if likely (x.small & 1L)
        return (Int_t){.small=(~x.small) ^ 3L};
    return Int$slow_negated(x);
}

MACROLIKE Int_t Int$negative(Int_t x) {
    if likely (x.small & 1L)
        return (Int_t){.small=((-((x.small)>>2L))<<2L) | 1L};
    return Int$slow_negative(x);
}

MACROLIKE PUREFUNC bool Int$is_negative(Int_t x) {
    if likely (x.small & 1L)
        return x.small < 0L;
    return Int$compare_value(x, I_small(0)) < 0L;
}

// Constructors/conversion functions:

// Int constructors:
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
MACROLIKE PUREFUNC Int_t Int$from_num(double n, bool truncate) {
    mpz_t result;
    mpz_init_set_d(result, n);
    if (!truncate && unlikely(mpz_get_d(result) != n))
        fail("Could not convert to an integer without truncation: ", n);
    return Int$from_mpz(result);
}
MACROLIKE PUREFUNC Int_t Int$from_num32(float n, bool truncate) { return Int$from_num((double)n, truncate); }
MACROLIKE Int_t Int$from_int64(int64_t i) {
    if likely (i >= SMALLEST_SMALL_INT && i <= BIGGEST_SMALL_INT)
        return (Int_t){.small=(i<<2L)|1L};
    mpz_t result;
    mpz_init_set_si(result, i);
    return Int$from_mpz(result);
}
MACROLIKE CONSTFUNC Int_t Int$from_int32(Int32_t i) { return Int$from_int64((Int32_t)i); }
MACROLIKE CONSTFUNC Int_t Int$from_int16(Int16_t i) { return I_small(i); }
MACROLIKE CONSTFUNC Int_t Int$from_int8(Int8_t i)   { return I_small(i); }
MACROLIKE CONSTFUNC Int_t Int$from_byte(Byte_t b) { return I_small(b); }
MACROLIKE CONSTFUNC Int_t Int$from_bool(Bool_t b) { return I_small(b); }

// Int64 constructors
MACROLIKE PUREFUNC Int64_t Int64$from_num(Num_t n, bool truncate) {
    int64_t i64 = (int64_t)n;
    if (!truncate && unlikely((Num_t)i64 != n))
        fail("Could not convert Num to Int64 without truncation: ", n);
    return i64;
}
MACROLIKE PUREFUNC Int64_t Int64$from_num32(Num32_t n, bool truncate) {
    int64_t i64 = (int64_t)n;
    if (!truncate && unlikely((Num32_t)i64 != n))
        fail("Could not convert Num32 to Int64 without truncation: ", n);
    return i64;
}
MACROLIKE PUREFUNC Int64_t Int64$from_int(Int_t i, bool truncate) {
    if likely (i.small & 1L)
        return (int64_t)(i.small >> 2L);
    if (!truncate && unlikely(!mpz_fits_slong_p(*i.big)))
        fail("Integer is too big to fit in a 64-bit integer: ", i);
    return mpz_get_si(*i.big);
}
MACROLIKE CONSTFUNC Int64_t Int64$from_int32(Int32_t i) { return (Int64_t)i; }
MACROLIKE CONSTFUNC Int64_t Int64$from_int16(Int16_t i) { return (Int64_t)i; }
MACROLIKE CONSTFUNC Int64_t Int64$from_int8(Int8_t i) { return (Int64_t)i; }

// Int32 constructors
MACROLIKE PUREFUNC Int32_t Int32$from_num(Num_t n, bool truncate) {
    int32_t i32 = (int32_t)n;
    if (!truncate && unlikely((Num_t)i32 != n))
        fail("Could not convert Num to Int32 without truncation: ", n);
    return i32;
}
MACROLIKE PUREFUNC Int32_t Int32$from_num32(Num32_t n, bool truncate) {
    int32_t i32 = (int32_t)n;
    if (!truncate && unlikely((Num32_t)i32 != n))
        fail("Could not convert Num32 to Int32 without truncation: ", n);
    return i32;
}
MACROLIKE PUREFUNC Int32_t Int32$from_int(Int_t i, bool truncate) {
    int64_t i64 = Int64$from_int(i, truncate);
    int32_t i32 = (int32_t)i64;
    if (!truncate && unlikely((int64_t)i32 != i64))
        fail("Integer is too big to fit in a 32-bit integer: ", i);
    return i32;
}
MACROLIKE PUREFUNC Int32_t Int32$from_int64(Int64_t i64, bool truncate) {
    int32_t i32 = (int32_t)i64;
    if (!truncate && unlikely((int64_t)i32 != i64))
        fail("Integer is too big to fit in a 32-bit integer: ", i64);
    return i32;
}
MACROLIKE CONSTFUNC Int32_t Int32$from_int16(Int16_t i) { return (Int32_t)i; }
MACROLIKE CONSTFUNC Int32_t Int32$from_int8(Int8_t i) { return (Int32_t)i; }

// Int16 constructors
MACROLIKE PUREFUNC Int16_t Int16$from_num(Num_t n, bool truncate) {
    int16_t i16 = (int16_t)n;
    if (!truncate && unlikely((Num_t)i16 != n))
        fail("Could not convert Num to Int16 without truncation: ", n);
    return i16;
}
MACROLIKE PUREFUNC Int16_t Int16$from_num32(Num32_t n, bool truncate) {
    int16_t i16 = (int16_t)n;
    if (!truncate && unlikely((Num32_t)i16 != n))
        fail("Could not convert Num32 to Int16 without truncation: ", (double)n);
    return i16;
}
MACROLIKE PUREFUNC Int16_t Int16$from_int(Int_t i, bool truncate) {
    int64_t i64 = Int64$from_int(i, truncate);
    int16_t i16 = (int16_t)i64;
    if (!truncate && unlikely((int64_t)i16 != i64))
        fail("Integer is too big to fit in a 16-bit integer!");
    return i16;
}
MACROLIKE PUREFUNC Int16_t Int16$from_int64(Int64_t i64, bool truncate) {
    int16_t i16 = (int16_t)i64;
    if (!truncate && unlikely((int64_t)i16 != i64))
        fail("Integer is too big to fit in a 16-bit integer: ", i64);
    return i16;
}
MACROLIKE PUREFUNC Int16_t Int16$from_int32(Int32_t i32, bool truncate) {
    int16_t i16 = (int16_t)i32;
    if (!truncate && unlikely((int32_t)i16 != i32))
        fail("Integer is too big to fit in a 16-bit integer: ", i32);
    return i16;
}
MACROLIKE CONSTFUNC Int16_t Int16$from_int8(Int8_t i) { return (Int16_t)i; }

// Int8 constructors
MACROLIKE PUREFUNC Int8_t Int8$from_num(Num_t n, bool truncate) {
    int8_t i8 = (int8_t)n;
    if (!truncate && unlikely((Num_t)i8 != n))
        fail("Could not convert Num to Int8 without truncation: ", n);
    return i8;
}
MACROLIKE PUREFUNC Int8_t Int8$from_num32(Num32_t n, bool truncate) {
    int8_t i8 = (int8_t)n;
    if (!truncate && unlikely((Num32_t)i8 != n))
        fail("Could not convert Num32 to Int8 without truncation: ", n);
    return i8;
}
MACROLIKE PUREFUNC Int8_t Int8$from_int(Int_t i, bool truncate) {
    int64_t i64 = Int64$from_int(i, truncate);
    int8_t i8 = (int8_t)i64;
    if (!truncate && unlikely((int64_t)i8 != i64))
        fail("Integer is too big to fit in an 8-bit integer!");
    return i8;
}
MACROLIKE PUREFUNC Int8_t Int8$from_int64(Int64_t i64, bool truncate) {
    int8_t i8 = (int8_t)i64;
    if (!truncate && unlikely((int64_t)i8 != i64))
        fail("Integer is too big to fit in a 8-bit integer: ", i64);
    return i8;
}
MACROLIKE PUREFUNC Int8_t Int8$from_int32(Int32_t i32, bool truncate) {
    int8_t i8 = (int8_t)i32;
    if (!truncate && unlikely((int32_t)i8 != i32))
        fail("Integer is too big to fit in a 8-bit integer: ", i32);
    return i8;
}
MACROLIKE PUREFUNC Int8_t Int8$from_int16(Int16_t i16, bool truncate) {
    int8_t i8 = (int8_t)i16;
    if (!truncate && unlikely((int16_t)i8 != i16))
        fail("Integer is too big to fit in a 8-bit integer: ", i16);
    return i8;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
