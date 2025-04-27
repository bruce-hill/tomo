// Integer type infos and methods
#include <stdio.h> // Must be before gmp.h

#include <ctype.h>
#include <gc.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "datatypes.h"
#include "integers.h"
#include "lists.h"
#include "optionals.h"
#include "print.h"
#include "siphash.h"
#include "text.h"
#include "types.h"

public int Int$print(FILE *f, Int_t i) {
    if (likely(i.small & 1L)) {
        return _print_int(f, (int64_t)((i.small)>>2L));
    } else {
        return gmp_fprintf(f, "%Zd", *i.big);
    }
}

static inline Text_t _int64_to_text(int64_t n)
{
    char buf[21] = {[20]=0}; // Big enough for INT64_MIN + '\0'
    char *p = &buf[19];
    bool negative = n < 0;

    if (n == 0)
        *(p--) = '0';

    for (; n > 0; n /= 10)
        *(p--) = '0' + (n % 10);

    if (negative)
        *(p--) = '-';

    return Text$from_strn(p + 1, (size_t)(&buf[19] - p));
}

public Text_t Int$value_as_text(Int_t i) {
    if (likely(i.small & 1L)) {
        return _int64_to_text(i.small >> 2L);
    } else {
        char *str = mpz_get_str(NULL, 10, *i.big);
        return Text$from_str(str);
    }
}

public Text_t Int$as_text(const void *i, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!i) return Text("Int");
    Text_t text = Int$value_as_text(*(Int_t*)i);
    if (colorize) text = Text$concat(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

static bool Int$is_none(const void *i, const TypeInfo_t *info)
{
    (void)info;
    return ((Int_t*)i)->small == 0L;
}

public PUREFUNC int32_t Int$compare_value(const Int_t x, const Int_t y) {
    if (likely(x.small & y.small & 1L))
        return (x.small > y.small) - (x.small < y.small);
    else if (x.small & 1)
        return -mpz_cmp_si(*y.big, x.small);
    else if (y.small & 1)
        return mpz_cmp_si(*x.big, y.small);
    else
        return x.big == y.big ? 0 : mpz_cmp(*x.big, *y.big);
}

public PUREFUNC int32_t Int$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Int$compare_value(*(Int_t*)x, *(Int_t*)y);
}

public PUREFUNC bool Int$equal_value(const Int_t x, const Int_t y) {
    if (likely((x.small | y.small) & 1L))
        return x.small == y.small;
    else
        return x.big == y.big ? 0 : (mpz_cmp(*x.big, *y.big) == 0);
}

public PUREFUNC bool Int$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Int$equal_value(*(Int_t*)x, *(Int_t*)y);
}

public CONSTFUNC Int_t Int$clamped(Int_t x, Int_t low, Int_t high) {
    return (Int$compare(&x, &low, &Int$info) <= 0) ? low : (Int$compare(&x, &high, &Int$info) >= 0 ? high : x);
}

public CONSTFUNC bool Int$is_between(const Int_t x, const Int_t low, const Int_t high) {
    return Int$compare_value(low, x) <= 0 && Int$compare_value(x, high) <= 0;
}

public PUREFUNC uint64_t Int$hash(const void *vx, const TypeInfo_t *info) {
    (void)info;
    Int_t *x = (Int_t*)vx;
    if (likely(x->small & 1L)) {
        return siphash24((void*)x, sizeof(Int_t));
    } else {
        char *str = mpz_get_str(NULL, 16, *x->big);
        return siphash24((void*)str, strlen(str));
    }
}

public Text_t Int$hex(Int_t i, Int_t digits_int, bool uppercase, bool prefix) {
    if (Int$is_negative(i))
        return Text$concat(Text("-"), Int$hex(Int$negative(i), digits_int, uppercase, prefix));

    if (likely(i.small & 1L)) {
        uint64_t u64 = (uint64_t)(i.small >> 2);
        return Text$from_str(String(hex(u64, .no_prefix=!prefix, .digits=Int32$from_int(digits_int, false), .uppercase=uppercase)));
    } else {
        char *str = mpz_get_str(NULL, 16, *i.big);
        if (uppercase) {
            for (char *c = str; *c; c++)
                *c = (char)toupper(*c);
        }
        int64_t digits = Int64$from_int(digits_int, false);
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

    if (likely(i.small & 1L)) {
        uint64_t u64 = (uint64_t)(i.small >> 2);
        return Text$from_str(String(oct(u64, .no_prefix=!prefix, .digits=Int32$from_int(digits_int, false))));
    } else {
        int64_t digits = Int64$from_int(digits_int, false);
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
    if (y.small & 1L) {
        if (y.small < 0L)
            mpz_sub_ui(result, result, (uint64_t)(-(y.small >> 2L)));
        else
            mpz_add_ui(result, result, (uint64_t)(y.small >> 2L));
    } else {
        mpz_add(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_minus(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1L) {
        if (y.small < 0L)
            mpz_add_ui(result, result, (uint64_t)(-(y.small >> 2L)));
        else
            mpz_sub_ui(result, result, (uint64_t)(y.small >> 2L));
    } else {
        mpz_sub(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_times(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1L)
        mpz_mul_si(result, result, y.small >> 2L);
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
        bool d_positive = likely(divisor.small & 1L) ? divisor.small > 0x1L : mpz_sgn(*divisor.big) > 0;
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
    mp_bitcnt_t bits = (mp_bitcnt_t)Int64$from_int(y, false);
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_mul_2exp(result, result, bits);
    return Int$from_mpz(result);
}

public Int_t Int$slow_right_shifted(Int_t x, Int_t y)
{
    mp_bitcnt_t bits = (mp_bitcnt_t)Int64$from_int(y, false);
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
    if (likely(x.small & 1L))
        return (Int_t){.small=4L*-((x.small)>>2L) + 1L};

    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_neg(result, result);
    return Int$from_mpz(result);
}

public Int_t Int$abs(Int_t x)
{
    if (likely(x.small & 1L))
        return (Int_t){.small=4L*labs((x.small)>>2L) + 1L};

    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_abs(result, result);
    return Int$from_mpz(result);
}

public Int_t Int$power(Int_t base, Int_t exponent)
{
    int64_t exp = Int64$from_int(exponent, false);
    if (unlikely(exp < 0))
        fail("Cannot take a negative power of an integer!");
    mpz_t result;
    mpz_init_set_int(result, base);
    mpz_pow_ui(result, result, (uint64_t)exp);
    return Int$from_mpz(result);
}

public Int_t Int$gcd(Int_t x, Int_t y)
{
    if (likely(x.small & y.small & 0x1L))
        return I_small(Int32$gcd(x.small >> 2L, y.small >> 2L));

    mpz_t result;
    mpz_init(result);
    if (x.small & 0x1L)
        mpz_gcd_ui(result, *y.big, (uint64_t)labs(x.small>>2L));
    else if (y.small & 0x1L)
        mpz_gcd_ui(result, *x.big, (uint64_t)labs(y.small>>2L));
    else
        mpz_gcd(result, *x.big, *y.big);
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

typedef struct {
    OptionalInt_t current, last;
    Int_t step;
} IntRange_t;

static OptionalInt_t _next_int(IntRange_t *info)
{
    OptionalInt_t i = info->current;
    if (!Int$is_none(&i, &Int$info)) {
        Int_t next = Int$plus(i, info->step);
        if (!Int$is_none(&info->last, &Int$info) && Int$compare_value(next, info->last) == Int$compare_value(info->step, I(0)))
            next = NONE_INT;
        info->current = next;
    }
    return i;
}

public PUREFUNC Closure_t Int$to(Int_t first, Int_t last, OptionalInt_t step) {
    IntRange_t *range = GC_MALLOC(sizeof(IntRange_t));
    range->current = first;
    range->last = last;
    range->step = Int$is_none(&step, &Int$info) ?
        Int$compare_value(last, first) >= 0 ? (Int_t){.small=(1L<<2L)|1L} : (Int_t){.small=(-1L>>2L)|1L}
        : step;
    return (Closure_t){.fn=_next_int, .userdata=range};
}

public PUREFUNC Closure_t Int$onward(Int_t first, Int_t step) {
    IntRange_t *range = GC_MALLOC(sizeof(IntRange_t));
    range->current = first;
    range->last = NONE_INT;
    range->step = step;
    return (Closure_t){.fn=_next_int, .userdata=range};
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
    if (unlikely(Int$compare_value(reps, I(9999)) > 0))
        fail("Number of prime-test repetitions should not be above 9999");
    int reps_int = Int32$from_int(reps, false);
    return (mpz_probab_prime_p(p, reps_int) != 0);
}

public Int_t Int$next_prime(Int_t x)
{
    mpz_t p;
    mpz_init_set_int(p, x);
    mpz_nextprime(p, p);
    return Int$from_mpz(p);
}

#if __GNU_MP_VERSION >= 6
#if __GNU_MP_VERSION_MINOR >= 3
public OptionalInt_t Int$prev_prime(Int_t x)
{
    mpz_t p;
    mpz_init_set_int(p, x);
    if (unlikely(mpz_prevprime(p, p) == 0))
        return NONE_INT;
    return Int$from_mpz(p);
}
#endif
#endif

public Int_t Int$choose(Int_t n, Int_t k)
{
    if unlikely (Int$compare_value(n, I_small(0)) < 0)
        fail("Negative inputs are not supported for choose()");

    mpz_t ret;
    mpz_init(ret);

    int64_t k_i64 = Int64$from_int(k, false);
    if unlikely (k_i64 < 0)
        fail("Negative inputs are not supported for choose()");

    if likely (n.small & 1L) {
        mpz_bin_uiui(ret, (unsigned long)(n.small >> 2L), (unsigned long)k_i64);
    } else {
        mpz_t n_mpz;
        mpz_init_set_int(n_mpz, n);
        mpz_bin_ui(ret, n_mpz, (unsigned long)k_i64);
    }
    return Int$from_mpz(ret);
}

public Int_t Int$factorial(Int_t n)
{
    mpz_t ret;
    mpz_init(ret);
    int64_t n_i64 = Int64$from_int(n, false);
    if unlikely (n_i64 < 0)
        fail("Factorials are not defined for negative numbers");
    mpz_fac_ui(ret, (unsigned long)n_i64);
    return Int$from_mpz(ret);
}

static void Int$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    Int_t i = *(Int_t*)obj;
    if (likely(i.small & 1L)) {
        fputc(0, out);
        int64_t i64 = i.small >> 2L;
        Int64$serialize(&i64, out, pointers, &Int64$info);
    } else {
        fputc(1, out);
        mpz_t n;
        mpz_init_set_int(n, *(Int_t*)obj);
        mpz_out_raw(out, n);
    }
}

static void Int$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    if (fgetc(in) == 0) {
        int64_t i = 0;
        Int64$deserialize(in, &i, pointers, &Int64$info);
        *(Int_t*)obj = (Int_t){.small=(i<<2L) | 1L};
    } else {
        mpz_t n;
        mpz_init(n);
        mpz_inp_raw(n, in);
        *(Int_t*)obj = Int$from_mpz(n);
    }
}

public const TypeInfo_t Int$info = {
    .size=sizeof(Int_t),
    .align=__alignof__(Int_t),
    .metamethods={
        .compare=Int$compare,
        .equal=Int$equal,
        .hash=Int$hash,
        .as_text=Int$as_text,
        .is_none=Int$is_none,
        .serialize=Int$serialize,
        .deserialize=Int$deserialize,
    },
};

public void Int64$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info)
{
    (void)info, (void)pointers;
    int64_t i = *(int64_t*)obj;
    uint64_t z = (uint64_t)((i << 1L) ^ (i >> 63L)); // Zigzag encode
    while (z >= 0x80L) {
        fputc((uint8_t)(z | 0x80L), out);
        z >>= 7L;
    }
    fputc((uint8_t)z, out);
}

public void Int64$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *info)
{
    (void)info, (void)pointers;
    uint64_t z = 0;
    for(size_t shift = 0; ; shift += 7) {
        uint8_t byte = (uint8_t)fgetc(in);
        z |= ((uint64_t)(byte & 0x7F)) << shift;
        if ((byte & 0x80) == 0) break;
    }
    *(int64_t*)outval = (int64_t)((z >> 1L) ^ -(z & 1L)); // Zigzag decode
}

public void Int32$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info) 
{
    (void)info, (void)pointers;
    int32_t i = *(int32_t*)obj;
    uint32_t z = (uint32_t)((i << 1) ^ (i >> 31)); // Zigzag encode
    while (z >= 0x80) {
        fputc((uint8_t)(z | 0x80), out);
        z >>= 7;
    }
    fputc((uint8_t)z, out);
}

public void Int32$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *info)
{
    (void)info, (void)pointers;
    uint32_t z = 0;
    for(size_t shift = 0; ; shift += 7) {
        uint8_t byte = (uint8_t)fgetc(in);
        z |= ((uint32_t)(byte & 0x7F)) << shift;
        if ((byte & 0x80) == 0) break;
    }
    *(int32_t*)outval = (int32_t)((z >> 1L) ^ -(z & 1L)); // Zigzag decode
}

// The space savings for smaller ints are not worth having:
#define Int16$serialize NULL
#define Int16$deserialize NULL
#define Int8$serialize NULL
#define Int8$deserialize NULL

#ifdef __TINYC__
#define __builtin_add_overflow(x, y, result) ({ *(result) = (x) + (y); false; })
#endif

#define DEFINE_INT_TYPE(c_type, KindOfInt, min_val, max_val, to_attr)\
    public Text_t KindOfInt ## $as_text(const void *i, bool colorize, const TypeInfo_t *info) { \
        (void)info; \
        if (!i) return Text(#KindOfInt); \
        Text_t text = _int64_to_text((int64_t)(*(c_type*)i)); \
        return colorize ? Texts(Text("\033[35m"), text, Text("\033[m")) : text; \
    } \
    public PUREFUNC int32_t KindOfInt ## $compare(const void *x, const void *y, const TypeInfo_t *info) { \
        (void)info; \
        return (*(c_type*)x > *(c_type*)y) - (*(c_type*)x < *(c_type*)y); \
    } \
    public PUREFUNC bool KindOfInt ## $equal(const void *x, const void *y, const TypeInfo_t *info) { \
        (void)info; \
        return *(c_type*)x == *(c_type*)y; \
    } \
    public CONSTFUNC bool KindOfInt ## $is_between(const c_type x, const c_type low, const c_type high) { \
        return low <= x && x <= high; \
    } \
    public CONSTFUNC c_type KindOfInt ## $clamped(c_type x, c_type min, c_type max) { \
        return x < min ? min : (x > max ? max : x); \
    } \
    public Text_t KindOfInt ## $hex(c_type i, Int_t digits_int, bool uppercase, bool prefix) { \
        Int_t as_int = Int$from_int64((int64_t)i); \
        return Int$hex(as_int, digits_int, uppercase, prefix); \
    } \
    public Text_t KindOfInt ## $octal(c_type i, Int_t digits_int, bool prefix) { \
        Int_t as_int = Int$from_int64((int64_t)i); \
        return Int$octal(as_int, digits_int, prefix); \
    } \
    public List_t KindOfInt ## $bits(c_type x) { \
        List_t bit_list = (List_t){.data=GC_MALLOC_ATOMIC(sizeof(bool[8*sizeof(c_type)])), .atomic=1, .stride=sizeof(bool), .length=8*sizeof(c_type)}; \
        bool *bits = bit_list.data + sizeof(c_type)*8; \
        for (size_t i = 0; i < 8*sizeof(c_type); i++) { \
            *(bits--) = x & 1; \
            x >>= 1; \
        } \
        return bit_list; \
    } \
    typedef struct { \
        Optional##KindOfInt##_t current, last; \
        KindOfInt##_t step; \
    } KindOfInt##Range_t; \
    static Optional##KindOfInt##_t _next_##KindOfInt(KindOfInt##Range_t *info) \
    { \
        Optional##KindOfInt##_t i = info->current; \
        if (!i.is_none) { \
            KindOfInt##_t next; bool overflow = __builtin_add_overflow(i.value, info->step, &next); \
            if (overflow || (!info->last.is_none && (info->step >= 0 ? next > info->last.value : next < info->last.value))) \
                info->current = (Optional##KindOfInt##_t){.is_none=true}; \
            else \
                info->current = (Optional##KindOfInt##_t){.value=next}; \
        } \
        return i; \
    } \
    public to_attr Closure_t KindOfInt ## $to(c_type first, c_type last, Optional ## KindOfInt ## _t step) { \
        KindOfInt##Range_t *range = GC_MALLOC(sizeof(KindOfInt##Range_t)); \
        range->current = (Optional##KindOfInt##_t){.value=first}; \
        range->last = (Optional##KindOfInt##_t){.value=last}; \
        range->step = step.is_none ? (last >= first ? 1 : -1) : step.value; \
        return (Closure_t){.fn=_next_##KindOfInt, .userdata=range}; \
    } \
    public to_attr Closure_t KindOfInt ## $onward(c_type first, c_type step) { \
        KindOfInt##Range_t *range = GC_MALLOC(sizeof(KindOfInt##Range_t)); \
        range->current = (Optional##KindOfInt##_t){.value=first}; \
        range->last = (Optional##KindOfInt##_t){.is_none=true}; \
        range->step = step; \
        return (Closure_t){.fn=_next_##KindOfInt, .userdata=range}; \
    } \
    public PUREFUNC Optional ## KindOfInt ## _t KindOfInt ## $parse(Text_t text) { \
        OptionalInt_t full_int = Int$parse(text); \
        if (full_int.small == 0L) return (Optional ## KindOfInt ## _t){.is_none=true}; \
        if (Int$compare_value(full_int, I(min_val)) < 0) { \
            return (Optional ## KindOfInt ## _t){.is_none=true}; \
        } \
        if (Int$compare_value(full_int, I(max_val)) > 0) { \
            return (Optional ## KindOfInt ## _t){.is_none=true}; \
        } \
        return (Optional ## KindOfInt ## _t){.value=KindOfInt##$from_int(full_int, true)}; \
    } \
    public CONSTFUNC c_type KindOfInt ## $gcd(c_type x, c_type y) { \
        if (x == 0 || y == 0) return 0; \
        x = KindOfInt##$abs(x); \
        y = KindOfInt##$abs(y); \
        while (x != y) { \
            if (x > y) x -= y; \
            else y -= x; \
        } \
        return x; \
    } \
    public const c_type KindOfInt##$min = min_val; \
    public const c_type KindOfInt##$max = max_val; \
    public const TypeInfo_t KindOfInt##$info = { \
        .size=sizeof(c_type), \
        .align=__alignof__(c_type), \
        .metamethods={ \
            .compare=KindOfInt##$compare, \
            .as_text=KindOfInt##$as_text, \
            .serialize=KindOfInt##$serialize, \
            .deserialize=KindOfInt##$deserialize, \
        }, \
    };

DEFINE_INT_TYPE(int64_t,  Int64, INT64_MIN, INT64_MAX, __attribute__(()))
DEFINE_INT_TYPE(int32_t,  Int32, INT32_MIN, INT32_MAX, CONSTFUNC)
DEFINE_INT_TYPE(int16_t,  Int16, INT16_MIN, INT16_MAX, CONSTFUNC)
DEFINE_INT_TYPE(int8_t,   Int8,  INT8_MIN,  INT8_MAX, CONSTFUNC)
#undef DEFINE_INT_TYPE

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
