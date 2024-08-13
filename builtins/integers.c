// Integer type infos and methods
#include <gc.h>
#include <gc/cord.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "datatypes.h"
#include "integers.h"
#include "text.h"
#include "types.h"
#include "SipHash/halfsiphash.h"

static gmp_randstate_t Int_rng = {};

public void Int$init_random(long seed)
{
    gmp_randinit_default(Int_rng);
    gmp_randseed_ui(Int_rng, (unsigned long)seed);
}

public Int_t Int$from_i64(int64_t i)
{
    if (i == (int32_t)i) return (Int_t){.small=(i*4)+1};
    mpz_t result;
    mpz_init_set_si(result, i);
    return Int$from_mpz(result);
}

public CORD Int$as_text(const Int_t *i, bool colorize, const TypeInfo *type) {
    (void)type;
    if (!i) return "Int";

    if (__builtin_expect(i->small & 1, 1)) {
        return CORD_asprintf(colorize ? "\x1b[35m%ld\x1b[33;2m\x1b[m" : "%ld", (i->small)>>2);
    } else {
        char *str = mpz_get_str(NULL, 10, *i->big);
        return CORD_asprintf(colorize ? "\x1b[35m%s\x1b[33;2m\x1b[m" : "%s", str);
    }
}

public int32_t Int$compare(const Int_t *x, const Int_t *y, const TypeInfo *type) {
    (void)type;
    if (__builtin_expect(((x->small & y->small) & 1) == 0, 0))
        return mpz_cmp(*x->big, *y->big);
    return (x->small > y->small) - (x->small < y->small);
}

public int32_t Int$compare_value(const Int_t x, const Int_t y) {
    CORD_printf("comparing values %r vs %r\n", Int$as_text(&x, true, NULL), Int$as_text(&y, true, NULL));
    printf("comparing values %p vs %p\n", x.big, y.big);
    if (__builtin_expect(((x.small & y.small) & 1) == 0, 0)) {
        printf("MPZ comparing\n");
        return mpz_cmp(*x.big, *y.big);
    }
    return (x.small > y.small) - (x.small < y.small);
}

public bool Int$equal(const Int_t *x, const Int_t *y, const TypeInfo *type) {
    (void)type;
    return x->small == y->small || (__builtin_expect(((x->small & y->small) & 1) == 0, 0) && mpz_cmp(*x->big, *y->big) == 0);
}

public bool Int$equal_value(const Int_t x, const Int_t y) {
    return x.small == y.small || (__builtin_expect(((x.small & y.small) & 1) == 0, 0) && mpz_cmp(*x.big, *y.big) == 0);
}

public bool Int$hash(const Int_t *x, const TypeInfo *type) {
    (void)type;
    uint32_t hash;
    if (__builtin_expect(x->small & 1, 1)) {
        halfsiphash(&x->small, sizeof(x->small), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));
    } else {
        char *str = mpz_get_str(NULL, 16, *x->big);
        halfsiphash(str, strlen(str), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));
    }
    return hash;
}

public CORD Int$hex(Int_t i, int64_t digits, bool uppercase, bool prefix) {
    const char *hex_fmt = uppercase ? (prefix ? "0x%0.*lX" : "%0.*lX") : (prefix ? "0x%0.*lx" : "%0.*lx");
    if (__builtin_expect(i.small & 1, 1)) {
        return CORD_asprintf(hex_fmt, (i.small)>>2);
    } else {
        CORD str = mpz_get_str(NULL, 16, *i.big);
        if (uppercase) str = Text$upper(str);
        if (digits > (int64_t)CORD_len(str))
            str = CORD_cat(str, CORD_chars('0', digits - CORD_len(str)));
        if (prefix) str = CORD_cat("0x", str);
        return str;
    }
}

public CORD Int$octal(Int_t i, int64_t digits, bool prefix) {
    const char *octal_fmt = prefix ? "0o%0.*lo" : "%0.*lo";
    if (__builtin_expect(i.small & 1, 1)) {
        return CORD_asprintf(octal_fmt, (int)digits, (uint64_t)(i.small >> 2));
    } else {
        CORD str = mpz_get_str(NULL, 8, *i.big);
        if (digits > (int64_t)CORD_len(str))
            str = CORD_cat(str, CORD_chars('0', digits - CORD_len(str)));
        if (prefix) str = CORD_cat("0o", str);
        return str;
    }
}

public Int_t Int$slow_plus(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1) {
        mpz_t y_mpz;
        mpz_init_set_int(y_mpz, y);
        mpz_add(result, result, y_mpz);
    } else {
        mpz_add(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_minus(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1) {
        mpz_t y_mpz;
        mpz_init_set_int(y_mpz, y);
        mpz_sub(result, result, y_mpz);
    } else {
        mpz_sub(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_times(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1) {
        mpz_t y_mpz;
        mpz_init_set_int(y_mpz, y);
        mpz_mul(result, result, y_mpz);
    } else {
        mpz_mul(result, result, *y.big);
    }
    return Int$from_mpz(result);
}

public Int_t Int$slow_divided_by(Int_t x, Int_t y) {
    mpz_t result;
    mpz_init_set_int(result, x);
    if (y.small & 1) {
        mpz_t y_mpz;
        mpz_init_set_int(y_mpz, y);
        mpz_cdiv_q(result, result, y_mpz);
    } else {
        mpz_cdiv_q(result, result, *y.big);
    }
    return Int$from_mpz(result);
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
    mp_bitcnt_t bits = (mp_bitcnt_t)Int$as_i64(y);
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_mul_2exp(result, result, bits);
    return Int$from_mpz(result);
}

public Int_t Int$slow_right_shifted(Int_t x, Int_t y)
{
    mp_bitcnt_t bits = (mp_bitcnt_t)Int$as_i64(y);
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
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_neg(result, result);
    return Int$from_mpz(result);
}

public Int_t Int$slow_abs(Int_t x)
{
    mpz_t result;
    mpz_init_set_int(result, x);
    mpz_abs(result, result);
    return Int$from_mpz(result);
}

public Int_t Int$random(Int_t min, Int_t max) {
    int32_t cmp = Int$compare(&min, &max, &$Int);
    if (cmp > 0)
        fail("Random minimum value (%r) is larger than the maximum value (%r)",
             Int$as_text(&min, false, &$Int), Int$as_text(&max, false, &$Int));
    if (cmp == 0) return min;

    mpz_t range_size;
    mpz_init_set_int(range_size, max);
    if (min.small & 1) {
        mpz_t min_mpz;
        mpz_init_set_si(min_mpz, min.small >> 2);
        mpz_sub(range_size, range_size, min_mpz);
    } else {
        mpz_sub(range_size, range_size, *min.big);
    }

    mpz_t r;
    mpz_init(r);
    mpz_urandomm(r, Int_rng, range_size);
    return Int$plus(min, Int$from_mpz(r));
}

public Range_t Int$to(Int_t from, Int_t to) {
    return (Range_t){from, to, Int$compare(&to, &from, &$Int) >= 0 ? (Int_t){.small=(1<<2)|1} : (Int_t){.small=(-1/4)|1}};
}

public Int_t Int$from_text(CORD text) {
    const char *str = CORD_to_const_char_star(text);
    mpz_t i;
    if (strncmp(str, "0x", 2) == 0) {
        mpz_init_set_str(i, str + 2, 16);
    } else if (strncmp(str, "0o", 2) == 0) {
        mpz_init_set_str(i, str + 2, 8);
    } else if (strncmp(str, "0b", 2) == 0) {
        mpz_init_set_str(i, str + 2, 2);
    } else {
        mpz_init_set_str(i, str, 10);
    }
    return Int$from_mpz(i);
}

public const TypeInfo $Int = {
    .size=sizeof(Int_t),
    .align=__alignof__(Int_t),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Int$compare,
        .equal=(void*)Int$equal,
        .hash=(void*)Int$equal,
        .as_text=(void*)Int$as_text,
    },
};


#define DEFINE_INT_TYPE(c_type, KindOfInt, fmt, min_val, max_val)\
    public CORD KindOfInt ## $as_text(const c_type *i, bool colorize, const TypeInfo *type) { \
        (void)type; \
        if (!i) return #KindOfInt; \
        CORD c; \
        if (colorize) CORD_sprintf(&c, "\x1b[35m%"fmt"\x1b[33;2m\x1b[m", *i); \
        else CORD_sprintf(&c, "%"fmt, *i); \
        return c; \
    } \
    public int32_t KindOfInt ## $compare(const c_type *x, const c_type *y, const TypeInfo *type) { \
        (void)type; \
        return (*x > *y) - (*x < *y); \
    } \
    public bool KindOfInt ## $equal(const c_type *x, const c_type *y, const TypeInfo *type) { \
        (void)type; \
        return *x == *y; \
    } \
    public CORD KindOfInt ## $format(c_type i, int64_t digits) { \
        return CORD_asprintf("%0*" fmt, (int)digits, i); \
    } \
    public CORD KindOfInt ## $hex(c_type i, int64_t digits, bool uppercase, bool prefix) { \
        const char *hex_fmt = uppercase ? (prefix ? "0x%0.*lX" : "%0.*lX") : (prefix ? "0x%0.*lx" : "%0.*lx"); \
        return CORD_asprintf(hex_fmt, (int)digits, (uint64_t)i); \
    } \
    public CORD KindOfInt ## $octal(c_type i, int64_t digits, bool prefix) { \
        const char *octal_fmt = prefix ? "0o%0.*lo" : "%0.*lo"; \
        return CORD_asprintf(octal_fmt, (int)digits, (uint64_t)i); \
    } \
    public array_t KindOfInt ## $bits(c_type x) { \
        array_t bit_array = (array_t){.data=GC_MALLOC_ATOMIC(sizeof(bool[8*sizeof(c_type)])), .atomic=1, .stride=sizeof(bool), .length=8*sizeof(c_type)}; \
        bool *bits = bit_array.data + sizeof(c_type)*8; \
        for (size_t i = 0; i < 8*sizeof(c_type); i++) { \
            *(bits--) = x & 1; \
            x >>= 1; \
        } \
        return bit_array; \
    } \
    public c_type KindOfInt ## $random(c_type min, c_type max) { \
        if (min > max) fail("Random minimum value (%ld) is larger than the maximum value (%ld)", min, max); \
        if (min == max) return min; \
        if (min == min_val && max == max_val) { \
            c_type r; \
            arc4random_buf(&r, sizeof(r)); \
            return r; \
        } \
        uint64_t range = (uint64_t)max - (uint64_t)min + 1; \
        uint64_t min_r = -range % range; \
        uint64_t r; \
        for (;;) { \
            arc4random_buf(&r, sizeof(r)); \
            if (r >= min_r) break; \
        } \
        return (c_type)((uint64_t)min + (r % range)); \
    } \
    public Range_t KindOfInt ## $to(c_type from, c_type to) { \
        return (Range_t){Int$from_i64(from), Int$from_i64(to), to >= from ? (Int_t){.small=(1<<2)&1} : (Int_t){.small=(1<<2)&1}}; \
    } \
    public c_type KindOfInt ## $from_text(CORD text, CORD *the_rest) { \
        const char *str = CORD_to_const_char_star(text); \
        long i; \
        char *end_ptr = NULL; \
        if (strncmp(str, "0x", 2) == 0) { \
            i = strtol(str, &end_ptr, 16); \
        } else if (strncmp(str, "0o", 2) == 0) { \
            i = strtol(str, &end_ptr, 8); \
        } else if (strncmp(str, "0b", 2) == 0) { \
            i = strtol(str, &end_ptr, 2); \
        } else { \
            i = strtol(str, &end_ptr, 10); \
        } \
        if (the_rest) *the_rest = CORD_from_char_star(end_ptr); \
        if (i < min_val) i = min_val; \
        else if (i > max_val) i = min_val; \
        return (c_type)i; \
    } \
    public const c_type KindOfInt##$min = min_val; \
    public const c_type KindOfInt##$max = max_val; \
    public const TypeInfo $ ## KindOfInt = { \
        .size=sizeof(c_type), \
        .align=__alignof__(c_type), \
        .tag=CustomInfo, \
        .CustomInfo={.compare=(void*)KindOfInt##$compare, .as_text=(void*)KindOfInt##$as_text}, \
    };

DEFINE_INT_TYPE(int64_t,  Int64,  "ld",     INT64_MIN, INT64_MAX);
DEFINE_INT_TYPE(int32_t,  Int32,  "d_i32",  INT32_MIN, INT32_MAX);
DEFINE_INT_TYPE(int16_t,  Int16,  "d_i16",  INT16_MIN, INT16_MAX);
DEFINE_INT_TYPE(int8_t,   Int8,   "d_i8",   INT8_MIN,  INT8_MAX);
#undef DEFINE_INT_TYPE

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
