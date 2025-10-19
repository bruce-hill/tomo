// Big integer type (`Int` in Tomo)

#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

Text_t Int$as_text(const void *i, bool colorize, const TypeInfo_t *type);
Text_t Int$value_as_text(Int_t i);
PUREFUNC uint64_t Int$hash(const void *x, const TypeInfo_t *type);
PUREFUNC int32_t Int$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC int32_t Int$compare_value(const Int_t x, const Int_t y);
CONSTFUNC bool Int$is_between(const Int_t x, const Int_t low, const Int_t high);
CONSTFUNC Int_t Int$clamped(Int_t x, Int_t low, Int_t high);
PUREFUNC bool Int$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Int$equal_value(const Int_t x, const Int_t y);
Text_t Int$hex(Int_t i, Int_t digits, bool uppercase, bool prefix);
Text_t Int$octal(Int_t i, Int_t digits, bool prefix);
PUREFUNC Closure_t Int$to(Int_t first, Int_t last, OptionalInt_t step);
PUREFUNC Closure_t Int$onward(Int_t first, Int_t step);
OptionalInt_t Int$from_str(const char *str);
OptionalInt_t Int$parse(Text_t text, Text_t *remainder);
Int_t Int$abs(Int_t x);
Int_t Int$power(Int_t base, Int_t exponent);
Int_t Int$gcd(Int_t x, Int_t y);
OptionalInt_t Int$sqrt(Int_t i);
bool Int$get_bit(Int_t x, Int_t bit_index);

#define BIGGEST_SMALL_INT 0x3fffffff
#define SMALLEST_SMALL_INT -0x40000000

#define Int$from_mpz(mpz)                                                                                              \
    (mpz_cmpabs_ui(mpz, BIGGEST_SMALL_INT) <= 0 ? ((Int_t){.small = (mpz_get_si(mpz) << 2L) | 1L})                     \
                                                : ((Int_t){.big = memcpy(new (mpz_t), &mpz, sizeof(mpz_t))}))

#define mpz_init_set_int(mpz, i)                                                                                       \
    do {                                                                                                               \
        if likely ((i).small & 1L) mpz_init_set_si(mpz, (i).small >> 2L);                                              \
        else mpz_init_set(mpz, *(i).big);                                                                              \
    } while (0)

#define I_small(i) ((Int_t){.small = (int64_t)((uint64_t)(i) << 2L) | 1L})
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
OptionalInt_t Int$prev_prime(Int_t x);
#endif
#endif
Int_t Int$choose(Int_t n, Int_t k);
Int_t Int$factorial(Int_t n);

extern const TypeInfo_t Int$info;

// Fast-path inline versions for the common case where integer arithmetic is
// between two small ints.

MACROLIKE Int_t Int$plus(Int_t x, Int_t y) {
    const int64_t z = (int64_t)((uint64_t)x.small + (uint64_t)y.small);
    if likely ((z | 2L) == (int32_t)z) return (Int_t){.small = (z - 1L)};
    return Int$slow_plus(x, y);
}

MACROLIKE Int_t Int$minus(Int_t x, Int_t y) {
    const int64_t z = (int64_t)(((uint64_t)x.small ^ 3L) - (uint64_t)y.small);
    if likely ((z & ~2L) == (int32_t)z) return (Int_t){.small = z};
    return Int$slow_minus(x, y);
}

MACROLIKE Int_t Int$times(Int_t x, Int_t y) {
    if likely ((x.small & y.small) & 1L) {
        const int64_t z = (x.small >> 1L) * (y.small >> 1L);
        if likely (z == (int32_t)z) return (Int_t){.small = z + 1L};
    }
    return Int$slow_times(x, y);
}

MACROLIKE Int_t Int$divided_by(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        // Euclidean division, see:
        // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small >> 2L);
        const int64_t d = (y.small >> 2L);
        int64_t q = D / d, r = D % d;
        q -= (r < 0L) * (2L * (d > 0L) - 1L);
        if likely (q == (int32_t)q) return (Int_t){.small = (q << 2L) | 1L};
    }
    return Int$slow_divided_by(x, y);
}

MACROLIKE Int_t Int$modulo(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        // Euclidean modulus, see:
        // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small >> 2L);
        const int64_t d = (y.small >> 2L);
        int64_t r = D % d;
        r -= (r < 0L) * (2L * (d < 0L) - 1L) * d;
        return (Int_t){.small = (r << 2L) | 1L};
    }
    return Int$slow_modulo(x, y);
}

MACROLIKE Int_t Int$modulo1(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        // Euclidean modulus, see:
        // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
        const int64_t D = (x.small >> 2L) - 1L;
        const int64_t d = (y.small >> 2L);
        int64_t r = D % d;
        r -= (r < 0L) * (2L * (d < 0L) - 1L) * d;
        return (Int_t){.small = ((r + 1L) << 2L) | 1L};
    }
    return Int$slow_modulo1(x, y);
}

MACROLIKE Int_t Int$left_shifted(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        const int64_t z = ((x.small >> 2L) << (y.small >> 2L)) << 2L;
        if likely (z == (int32_t)z) return (Int_t){.small = z + 1L};
    }
    return Int$slow_left_shifted(x, y);
}

MACROLIKE Int_t Int$right_shifted(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) {
        const int64_t z = ((x.small >> 2L) >> (y.small >> 2L)) << 2L;
        if likely (z == (int32_t)z) return (Int_t){.small = z + 1L};
    }
    return Int$slow_right_shifted(x, y);
}

MACROLIKE Int_t Int$bit_and(Int_t x, Int_t y) {
    const int64_t z = x.small & y.small;
    if likely (z & 1L) return (Int_t){.small = z};
    return Int$slow_bit_and(x, y);
}

MACROLIKE Int_t Int$bit_or(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) return (Int_t){.small = (x.small | y.small)};
    return Int$slow_bit_or(x, y);
}

MACROLIKE Int_t Int$bit_xor(Int_t x, Int_t y) {
    if likely (x.small & y.small & 1L) return (Int_t){.small = (x.small ^ y.small) | 1L};
    return Int$slow_bit_xor(x, y);
}

MACROLIKE Int_t Int$negated(Int_t x) {
    if likely (x.small & 1L) return (Int_t){.small = (~x.small) ^ 3L};
    return Int$slow_negated(x);
}

MACROLIKE Int_t Int$negative(Int_t x) {
    if likely (x.small & 1L) return (Int_t){.small = ((-((x.small) >> 2L)) << 2L) | 1L};
    return Int$slow_negative(x);
}

MACROLIKE PUREFUNC bool Int$is_negative(Int_t x) {
    if likely (x.small & 1L) return x.small < 0L;
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
    if (!truncate && unlikely(mpz_get_d(result) != n)) fail("Could not convert to an integer without truncation: ", n);
    return Int$from_mpz(result);
}
MACROLIKE PUREFUNC Int_t Int$from_num32(float n, bool truncate) { return Int$from_num((double)n, truncate); }
MACROLIKE Int_t Int$from_int64(int64_t i) {
    if likely (i >= SMALLEST_SMALL_INT && i <= BIGGEST_SMALL_INT) return (Int_t){.small = (i << 2L) | 1L};
    mpz_t result;
    mpz_init_set_si(result, i);
    return Int$from_mpz(result);
}
MACROLIKE CONSTFUNC Int_t Int$from_int32(Int32_t i) { return Int$from_int64((Int32_t)i); }
MACROLIKE CONSTFUNC Int_t Int$from_int16(Int16_t i) { return I_small(i); }
MACROLIKE CONSTFUNC Int_t Int$from_int8(Int8_t i) { return I_small(i); }
MACROLIKE CONSTFUNC Int_t Int$from_byte(Byte_t b) { return I_small(b); }
MACROLIKE CONSTFUNC Int_t Int$from_bool(Bool_t b) { return I_small(b); }

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
