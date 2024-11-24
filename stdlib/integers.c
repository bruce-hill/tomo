// Integer type infos and methods
#include <ctype.h>
#include <gc.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "arrays.h"
#include "datatypes.h"
#include "integers.h"
#include "optionals.h"
#include "siphash.h"
#include "text.h"
#include "types.h"

public Text_t Int$value_as_text(Int_t i) {
    if (__builtin_expect(i.small & 1, 1)) {
        return Text$format("%ld", (i.small)>>2);
    } else {
        char *str = mpz_get_str(NULL, 10, *i.big);
        return Text$from_str(str);
    }
}

public Text_t Int$as_text(const Int_t *i, bool colorize, const TypeInfo_t *type) {
    (void)type;
    if (!i) return Text("Int");

    Text_t text = Int$value_as_text(*i);
    if (colorize) text = Text$concat(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

public PUREFUNC int32_t Int$compare(const Int_t *x, const Int_t *y, const TypeInfo_t *type) {
    (void)type;
    if (__builtin_expect(((x->small | y->small) & 1) == 0, 0))
        return x->big == y->big ? 0 : mpz_cmp(*x->big, *y->big);
    return (x->small > y->small) - (x->small < y->small);
}

public PUREFUNC int32_t Int$compare_value(const Int_t x, const Int_t y) {
    if (__builtin_expect(((x.small | y.small) & 1) == 0, 0))
        return x.big == y.big ? 0 : mpz_cmp(*x.big, *y.big);
    return (x.small > y.small) - (x.small < y.small);
}

public PUREFUNC bool Int$equal(const Int_t *x, const Int_t *y, const TypeInfo_t *type) {
    (void)type;
    return x->small == y->small || (__builtin_expect(((x->small | y->small) & 1) == 0, 0) && mpz_cmp(*x->big, *y->big) == 0);
}

public PUREFUNC bool Int$equal_value(const Int_t x, const Int_t y) {
    return x.small == y.small || (__builtin_expect(((x.small | y.small) & 1) == 0, 0) && mpz_cmp(*x.big, *y.big) == 0);
}

public PUREFUNC uint64_t Int$hash(const Int_t *x, const TypeInfo_t *type) {
    (void)type;
    if (__builtin_expect(x->small & 1, 1)) {
        return siphash24((void*)x, sizeof(Int_t));
    } else {
        char *str = mpz_get_str(NULL, 16, *x->big);
        return siphash24((void*)str, strlen(str));
    }
}

public Text_t Int$format(Int_t i, Int_t digits_int) {
    int64_t digits = Int_to_Int64(digits_int, false);
    if (__builtin_expect(i.small & 1, 1)) {
        return Text$format("%0.*ld", digits, (i.small)>>2);
    } else {
        char *str = mpz_get_str(NULL, 10, *i.big);
        bool negative = (str[0] == '-');
        int64_t needed_zeroes = digits - (int64_t)strlen(str);
        if (needed_zeroes <= 0)
            return Text$from_str(str);

        char *zeroes = GC_MALLOC_ATOMIC((size_t)(needed_zeroes));
        memset(zeroes, '0', (size_t)(needed_zeroes));
        if (negative)
            return Text$concat(Text("-"), Text$from_str(zeroes), Text$from_str(str + 1));
        else
            return Text$concat(Text$from_str(zeroes), Text$from_str(str));
    }
}

public Text_t Int$hex(Int_t i, Int_t digits_int, bool uppercase, bool prefix) {
    if (Int$is_negative(i))
        return Text$concat(Text("-"), Int$hex(Int$negative(i), digits_int, uppercase, prefix));

    int64_t digits = Int_to_Int64(digits_int, false);
    if (__builtin_expect(i.small & 1, 1)) {
        const char *hex_fmt = uppercase ? (prefix ? "0x%0.*lX" : "%0.*lX") : (prefix ? "0x%0.*lx" : "%0.*lx");
        return Text$format(hex_fmt, digits, (i.small)>>2);
    } else {
        char *str = mpz_get_str(NULL, 16, *i.big);
        if (uppercase) {
            for (char *c = str; *c; c++)
                *c = (char)toupper(*c);
        }
        int64_t needed_zeroes = digits - (int64_t)strlen(str);
        if (needed_zeroes <= 0)
            return prefix ? Text$concat(Text("0x"), Text$from_str(str)) : Text$from_str(str);

        char *zeroes = GC_MALLOC_ATOMIC((size_t)(needed_zeroes));
        memset(zeroes, '0', (size_t)(needed_zeroes));
        if (prefix)
            return Text$concat(Text("0x"), Text$from_str(zeroes), Text$from_str(str));
        else
            return Text$concat(Text$from_str(zeroes), Text$from_str(str));
    }
}

public Text_t Int$octal(Int_t i, Int_t digits_int, bool prefix) {
    if (Int$is_negative(i))
        return Text$concat(Text("-"), Int$octal(Int$negative(i), digits_int, prefix));

    int64_t digits = Int_to_Int64(digits_int, false);
    if (__builtin_expect(i.small & 1, 1)) {
        const char *octal_fmt = prefix ? "0o%0.*lo" : "%0.*lo";
        return Text$format(octal_fmt, digits, (i.small)>>2);
    } else {
        char *str = mpz_get_str(NULL, 8, *i.big);
        int64_t needed_zeroes = digits - (int64_t)strlen(str);
        if (needed_zeroes <= 0)
            return prefix ? Text$concat(Text("0o"), Text$from_str(str)) : Text$from_str(str);

        char *zeroes = GC_MALLOC_ATOMIC((size_t)(needed_zeroes));
        memset(zeroes, '0', (size_t)(needed_zeroes));
        if (prefix)
            return Text$concat(Text("0o"), Text$from_str(zeroes), Text$from_str(str));
        else
            return Text$concat(Text$from_str(zeroes), Text$from_str(str));
    }
}

public Int_t Int$slow_plus(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1) {
        if (y.small < 0)
            mpz_sub_ui(result, result, (uint64_t)(-(y.small >> 2)));
        else
            mpz_add_ui(result, result, (uint64_t)(y.small >> 2));
    } else {
        mpz_add(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_minus(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1) {
        if (y.small < 0)
            mpz_add_ui(result, result, (uint64_t)(-(y.small >> 2)));
        else
            mpz_sub_ui(result, result, (uint64_t)(y.small >> 2));
    } else {
        mpz_sub(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_times(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1)
        mpz_mul_si(result, result, y.small >> 2);
    else
        mpz_mul(result, result, *y.big);
    return Int$from_mpz(result);
}

public Int_t Int$slow_divided_by(Int_t dividend, Int_t divisor) {
    // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    mpz_t quotient, remainder;
    mpz_init_set_int(quotient, dividend);
    mpz_init_set_int(remainder, divisor);
    mpz_tdiv_qr(quotient, remainder, quotient, remainder);
    if (mpz_sgn(remainder) < 0) {
        bool d_positive = __builtin_expect(divisor.small & 1, 1) ? divisor.small > 0x1 : mpz_sgn(*divisor.big) > 0;
        if (d_positive)
            mpz_sub_ui(quotient, quotient, 1);
        else
            mpz_add_ui(quotient, quotient, 1);
    }
    return Int$from_mpz(quotient);
}

public Int_t Int$slow_modulo(Int_t x, Int_t modulus)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_t divisor;
    mpz_init_set_int(divisor, modulus);
    mpz_mod(result, result, divisor);
    return Int$from_mpz(result);
}

public Int_t Int$slow_modulo1(Int_t x, Int_t modulus)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_sub_ui(result, result, 1);
    mpz_t divisor;
    mpz_init_set_int(divisor, modulus);
    mpz_mod(result, result, divisor);
    mpz_add_ui(result, result, 1);
    return Int$from_mpz(result);
}

public Int_t Int$slow_left_shifted(Int_t x, Int_t y)
{
    mp_bitcnt_t bits = (mp_bitcnt_t)Int_to_Int64(y, false);
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_mul_2exp(result, result, bits);
    return Int$from_mpz(result);
}

public Int_t Int$slow_right_shifted(Int_t x, Int_t y)
{
    mp_bitcnt_t bits = (mp_bitcnt_t)Int_to_Int64(y, false);
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_tdiv_q_2exp(result, result, bits);
    return Int$from_mpz(result);
}

public Int_t Int$slow_bit_and(Int_t x, Int_t y)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_t y_mpz;
    mpz_init_set_int(y_mpz, y);
    mpz_and(result, result, y_mpz);
    return Int$from_mpz(result);
}

public Int_t Int$slow_bit_or(Int_t x, Int_t y)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_t y_mpz;
    mpz_init_set_int(y_mpz, y);
    mpz_ior(result, result, y_mpz);
    return Int$from_mpz(result);
}

public Int_t Int$slow_bit_xor(Int_t x, Int_t y)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_t y_mpz;
    mpz_init_set_int(y_mpz, y);
    mpz_xor(result, result, y_mpz);
    return Int$from_mpz(result);
}

public Int_t Int$slow_negated(Int_t x)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_neg(result, result);
    mpz_sub_ui(result, result, 1);
    return Int$from_mpz(result);
}

public Int_t Int$slow_negative(Int_t x)
{
    if (__builtin_expect((x.small & 1), 1))
        return (Int_t){.small=4*-((x.small)>>2) + 1};

    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_neg(result, result);
    return Int$from_mpz(result);
}

public Int_t Int$abs(Int_t x)
{
    if (__builtin_expect((x.small & 1), 1))
        return (Int_t){.small=4*labs((x.small)>>2) + 1};

    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_abs(result, result);
    return Int$from_mpz(result);
}

public Int_t Int$power(Int_t base, Int_t exponent)
{
    int64_t exp = Int_to_Int64(exponent, false);
    if (__builtin_expect(exp < 0, 0))
        fail("Cannot take a negative power of an integer!");
    mpz_t result;
    mpz_init_set_int(result, base);
    mpz_pow_ui(result, result, (uint64_t)exp);
    return Int$from_mpz(result);
}

public OptionalInt_t Int$sqrt(Int_t i)
{
    if (Int$compare_value(i, I(0)) < 0)
        return NONE_INT;
    mpz_t result;
    mpz_init_set_int(result, i);
    mpz_sqrt(result, result);
    return Int$from_mpz(result);
}

public PUREFUNC Range_t Int$to(Int_t from, Int_t to) {
    return (Range_t){from, to, Int$compare_value(to, from) >= 0 ? (Int_t){.small=(1<<2)|1} : (Int_t){.small=(-1>>2)|1}};
}

public Int_t Int$from_str(const char *str) {
    mpz_t i;
    int result;
    if (strncmp(str, "0x", 2) == 0) {
        result = mpz_init_set_str(i, str + 2, 16);
    } else if (strncmp(str, "0o", 2) == 0) {
        result = mpz_init_set_str(i, str + 2, 8);
    } else if (strncmp(str, "0b", 2) == 0) {
        result = mpz_init_set_str(i, str + 2, 2);
    } else {
        result = mpz_init_set_str(i, str, 10);
    }
    if (result != 0)
        return NONE_INT;
    return Int$from_mpz(i);
}

public OptionalInt_t Int$parse(Text_t text) {
    return Int$from_str(Text$as_c_string(text));
}

public bool Int$is_prime(Int_t x, Int_t reps)
{
    mpz_t p;
    mpz_init_set_int(p, x);
    if (Int$compare_value(reps, I(9999)) > 0)
        fail("Number of prime-test repetitions should not be above 9999");
    int reps_int = Int_to_Int32(reps, false);
    return (mpz_probab_prime_p(p, reps_int) != 0);
}

public Int_t Int$next_prime(Int_t x)
{
    mpz_t p;
    mpz_init_set_int(p, x);
    mpz_nextprime(p, p);
    return Int$from_mpz(p);
}

public Int_t Int$prev_prime(Int_t x)
{
    mpz_t p;
    mpz_init_set_int(p, x);
    if (mpz_prevprime(p, p) == 0)
        fail("There is no prime number before %k", (Text_t[1]){Int$as_text(&x, false, &Int$info)});
    return Int$from_mpz(p);
}

public const TypeInfo_t Int$info = {
    .size=sizeof(Int_t),
    .align=__alignof__(Int_t),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Int$compare,
        .equal=(void*)Int$equal,
        .hash=(void*)Int$hash,
        .as_text=(void*)Int$as_text,
    },
};

#define DEFINE_INT_TYPE(c_type, KindOfInt, fmt, min_val, max_val, to_attr)\
    public Text_t KindOfInt ## $as_text(const c_type *i, bool colorize, const TypeInfo_t *type) { \
        (void)type; \
        if (!i) return Text(#KindOfInt); \
        return Text$format(colorize ? "\x1b[35m" fmt "\x1b[m" : fmt, *i); \
    } \
    public PUREFUNC int32_t KindOfInt ## $compare(const c_type *x, const c_type *y, const TypeInfo_t *type) { \
        (void)type; \
        return (*x > *y) - (*x < *y); \
    } \
    public PUREFUNC bool KindOfInt ## $equal(const c_type *x, const c_type *y, const TypeInfo_t *type) { \
        (void)type; \
        return *x == *y; \
    } \
    public Text_t KindOfInt ## $format(c_type i, Int_t digits_int) { \
        Int_t as_int = KindOfInt##_to_Int(i); \
        return Int$format(as_int, digits_int); \
    } \
    public Text_t KindOfInt ## $hex(c_type i, Int_t digits_int, bool uppercase, bool prefix) { \
        Int_t as_int = KindOfInt##_to_Int(i); \
        return Int$hex(as_int, digits_int, uppercase, prefix); \
    } \
    public Text_t KindOfInt ## $octal(c_type i, Int_t digits_int, bool prefix) { \
        Int_t as_int = KindOfInt##_to_Int(i); \
        return Int$octal(as_int, digits_int, prefix); \
    } \
    public Array_t KindOfInt ## $bits(c_type x) { \
        Array_t bit_array = (Array_t){.data=GC_MALLOC_ATOMIC(sizeof(bool[8*sizeof(c_type)])), .atomic=1, .stride=sizeof(bool), .length=8*sizeof(c_type)}; \
        bool *bits = bit_array.data + sizeof(c_type)*8; \
        for (size_t i = 0; i < 8*sizeof(c_type); i++) { \
            *(bits--) = x & 1; \
            x >>= 1; \
        } \
        return bit_array; \
    } \
    public to_attr Range_t KindOfInt ## $to(c_type from, c_type to) { \
        return (Range_t){Int64_to_Int(from), Int64_to_Int(to), to >= from ? I_small(1): I_small(-1)}; \
    } \
    public PUREFUNC Optional ## KindOfInt ## _t KindOfInt ## $parse(Text_t text) { \
        OptionalInt_t full_int = Int$parse(text); \
        if (full_int.small == 0) return (Optional ## KindOfInt ## _t){.is_null=true}; \
        if (Int$compare_value(full_int, I(min_val)) < 0) { \
            return (Optional ## KindOfInt ## _t){.is_null=true}; \
        } \
        if (Int$compare_value(full_int, I(max_val)) > 0) { \
            return (Optional ## KindOfInt ## _t){.is_null=true}; \
        } \
        return (Optional ## KindOfInt ## _t){.i=Int_to_ ## KindOfInt(full_int, true)}; \
    } \
    public const c_type KindOfInt##$min = min_val; \
    public const c_type KindOfInt##$max = max_val; \
    public const TypeInfo_t KindOfInt##$info = { \
        .size=sizeof(c_type), \
        .align=__alignof__(c_type), \
        .tag=CustomInfo, \
        .CustomInfo={.compare=(void*)KindOfInt##$compare, .as_text=(void*)KindOfInt##$as_text}, \
    };

DEFINE_INT_TYPE(int64_t,  Int64,  "%ld", INT64_MIN, INT64_MAX, __attribute__(()))
DEFINE_INT_TYPE(int32_t,  Int32,  "%d",  INT32_MIN, INT32_MAX, CONSTFUNC)
DEFINE_INT_TYPE(int16_t,  Int16,  "%d",  INT16_MIN, INT16_MAX, CONSTFUNC)
DEFINE_INT_TYPE(int8_t,   Int8,   "%d",  INT8_MIN,  INT8_MAX, CONSTFUNC)
#undef DEFINE_INT_TYPE

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
