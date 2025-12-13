#include <gc.h>
#include <gmp.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "bigint.h"
#include "datatypes.h"
#include "reals.h"
#include "text.h"
#include "types.h"

struct ieee754_bits {
    bool negative : 1;
    uint64_t biased_exponent : 11;
    uint64_t fraction : 53;
};

#define pow10(x, n) Int$times(x, Int$power(I(10), I(n)))

public
Int_t Real$compute(Real_t r, int64_t decimals) {
    if (r->approximation.small != 0 && r->approximation_decimals >= decimals) {
        return r->approximation_decimals == decimals ? r->approximation
                                                     : pow10(r->approximation, decimals - r->approximation_decimals);
    }

    r->approximation = r->compute(r, decimals);
    r->approximation_decimals = decimals;
    return r->approximation;
}

static int64_t approx_log10(Real_t r, int64_t decimals) {
    if ((r->approximation.small | 0x1) == 0x1) {
        (void)Real$compute(r, decimals);
    }

    if ((r->approximation.small & 0x1) == 0x1) {
        int64_t small = (r->approximation.small) >> 2;
        if (small < 0) small = -small;
        int64_t leading_zeroes = (int64_t)__builtin_clzl((uint64_t)labs(small));
        return (64 - leading_zeroes) + r->approximation_decimals;
    } else {
        size_t digits = mpz_sizeinbase(r->approximation.big, 10);
        return (int64_t)digits + r->approximation_decimals;
    }
}

static Int_t Real$compute_int(Real_t r, int64_t decimals) { return pow10(r->userdata.i, decimals); }

static Int_t Real$compute_double(Real_t r, int64_t decimals) {
    // TODO: this is probably inaccurate
    return Int$from_float64(r->userdata.n * pow(10, (double)decimals), true);
}

public
OptionalReal_t Real$parse(Text_t text, Text_t *remainder) {
    Text_t decimal_onwards = EMPTY_TEXT;
    OptionalInt_t int_component = Int$parse(text, I(10), &decimal_onwards);
    if (int_component.small == 0) int_component = I(0);
    Text_t fraction_text = EMPTY_TEXT;
    if (Text$starts_with(decimal_onwards, Text("."), &fraction_text)) {
        fraction_text = Text$replace(fraction_text, Text("_"), EMPTY_TEXT);
        OptionalInt_t fraction;
        if (fraction_text.length == 0) {
            fraction = I(0);
        } else {
            fraction = Int$parse(fraction_text, I(10), remainder);
            if (fraction.small == 0) return NONE_REAL;
        }
        int64_t shift = fraction_text.length;
        Int_t scale = Int$power(I(10), I(shift));
        Int_t i = Int$plus(Int$times(int_component, scale), fraction);
        Real_t ret = Real$divided_by(Real$from_int(i), Real$from_int(scale));
        ret->approximation = i;
        ret->approximation_decimals = shift;
        return ret;
    } else {
        if (decimal_onwards.length > 0) {
            if (remainder) *remainder = decimal_onwards;
            else return NONE_REAL;
        }
        return Real$from_int(int_component);
    }
}

public
Real_t Real$from_float64(double n) { return new (struct Real_s, .compute = Real$compute_double, .userdata.n = n); }

public
double Real$as_float64(Real_t x) {
    int64_t decimals = 17;
    Int_t i = Real$compute(x, decimals);
    mpq_t q;
    mpz_t num;
    mpz_init_set_int(num, i);
    mpz_t den;
    mpz_init_set_int(den, Int$power(I(10), I(decimals)));
    mpq_set_num(q, num);
    mpq_set_den(q, den);
    return mpq_get_d(q);
}

public
Real_t Real$from_int(Int_t i) {
    return new (struct Real_s, .compute = Real$compute_int, .userdata.i = i, .approximation = i, .exact = 1,
                .approximation_decimals = 0);
}

static Int_t Real$compute_negative(Real_t r, int64_t decimals) {
    Int_t x = Real$compute(&r->userdata.children[0], decimals);
    return Int$negative(x);
}

public
Real_t Real$negative(Real_t x) { return new (struct Real_s, .compute = Real$compute_negative, .userdata.children = x); }

static Int_t Real$compute_plus(Real_t r, int64_t decimals) {
    Int_t lhs = Real$compute(&r->userdata.children[0], decimals + 1);
    Int_t rhs = Real$compute(&r->userdata.children[1], decimals + 1);
    return Int$divided_by(Int$plus(lhs, rhs), I(10));
}

public
Real_t Real$plus(Real_t x, Real_t y) {
    Real_t result =
        new (struct Real_s, .compute = Real$compute_plus, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])), );
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

static Int_t Real$compute_minus(Real_t r, int64_t decimals) {
    Int_t lhs = Real$compute(&r->userdata.children[0], decimals + 1);
    Int_t rhs = Real$compute(&r->userdata.children[1], decimals + 1);
    return Int$divided_by(Int$minus(lhs, rhs), I(10));
}

public
Real_t Real$minus(Real_t x, Real_t y) {
    Real_t result =
        new (struct Real_s, .compute = Real$compute_minus, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])), );
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

static Int_t Real$compute_times(Real_t r, int64_t decimals) {
    Real_t lhs = &r->userdata.children[0];
    Real_t rhs = &r->userdata.children[1];

    int64_t half_prec = decimals / 2;

    int64_t lhs_decimals = approx_log10(lhs, half_prec);
    int64_t rhs_decimals = approx_log10(rhs, half_prec);

    Real_t big, small;
    if (lhs_decimals >= rhs_decimals) big = lhs, small = rhs;
    else big = rhs, small = lhs;

    Int_t approx_small = Real$compute(small, decimals - MAX(lhs_decimals, rhs_decimals) - 3);
    if (approx_small.small == 0x1) return I(0);

    Int_t approx_big = Real$compute(big, decimals - MIN(lhs_decimals, rhs_decimals) - 3);
    if (approx_big.small == 0x1) return I(0);

    return Int$right_shifted(Int$times(approx_big, approx_small),
                             Int$from_int64(lhs_decimals + rhs_decimals - decimals));
}

public
Real_t Real$times(Real_t x, Real_t y) {
    // Simplification rules:
    if (x->compute == Real$compute_int && y->compute == Real$compute_int) {
        return Real$from_int(Int$times(x->userdata.i, y->userdata.i));
    } else if (x->compute == Real$compute_times && y->compute == Real$compute_int) {
        if (x->userdata.children[0].compute == Real$compute_int)
            return Real$times(Real$times(&x->userdata.children[0], y), &x->userdata.children[1]);
        else if (x->userdata.children[1].compute == Real$compute_int)
            return Real$times(Real$times(&x->userdata.children[1], y), &x->userdata.children[0]);
    }

    Real_t result =
        new (struct Real_s, .compute = Real$compute_times, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])));

    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

// static Int_t Real$compute_inverse(Real_t r, int64_t decimals) {
//     Real_t op = &r->userdata.children[0];
//     int64_t magnitude = approx_log10(op, 100);
//     int64_t inv_magnitude = 1 - magnitude;
//     int64_t digits_needed = inv_magnitude - decimals + 3;
//     int64_t prec_needed = magnitude - digits_needed;
//     int64_t log_scale_factor = -decimals - prec_needed;
//     if (log_scale_factor < 0) return I(0);
//     Int_t dividend = Int$left_shifted(I(1), I(log_scale_factor));
//     Int_t scaled_divisor = Real$compute(op, prec_needed);
//     Int_t abs_scaled_divisor = Int$abs(scaled_divisor);
//     Int_t adj_dividend = Int$plus(dividend, Int$right_shifted(abs_scaled_divisor, I(1)));
//     // Adjustment so that final result is rounded.
//     Int_t result = Int$divided_by(adj_dividend, abs_scaled_divisor);

// if (Int$compare_value(scaled_divisor, I(0)) < 0) {
//     return Int$negative(result);
// } else {
//     return result;
// }

//     return r->approximation;
// }

// public
// Real_t Real$inverse(Real_t x) { return new (struct Real_s, .compute = Real$compute_inverse, .userdata.children = x);
// }

static Int_t Real$compute_divided_by(Real_t r, int64_t decimals) {
    int64_t den_mag = approx_log10(&r->userdata.children[1], 100);
    Int_t num = Real$compute(&r->userdata.children[0], decimals * 2 - den_mag);
    Int_t den = Real$compute(&r->userdata.children[1], decimals - den_mag);
    return Int$divided_by(num, den);
}

public
Real_t Real$divided_by(Real_t x, Real_t y) {
    // Exact integer division:
    if (x->compute == Real$compute_int && y->compute == Real$compute_int) {
        Int_t int_result = Int$divided_by(x->userdata.i, y->userdata.i);
        if (Int$equal_value(x->userdata.i, Int$times(int_result, y->userdata.i))) {
            return Real$from_int(int_result);
        }
    }
    Real_t result = new (struct Real_s, .compute = Real$compute_divided_by,
                         .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])), );
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

static Int_t Real$compute_sqrt(Real_t r, int64_t decimals) {
    Real_t operand = &r->userdata.children[0];
    double d = Real$as_float64(operand);
    // TODO: newton's method to iterate
    return Int$from_float64(sqrt(d) * pow(10.0, (double)decimals), true);
}

public
Real_t Real$sqrt(Real_t x) { return new (struct Real_s, .compute = Real$compute_sqrt, .userdata.children = x); }

public
Text_t Real$value_as_text(Real_t x, int64_t digits) {
    Int_t scaled_int = Real$compute(x, digits);
    Text_t scaled_string = Int$value_as_text(Int$abs(scaled_int));
    Text_t result;
    if (digits == 0) {
        result = scaled_string;
    } else {
        int64_t len = scaled_string.length;
        if (len <= digits) {
            // Add sufficient leading zeroes
            Text_t z = Text$repeat(Text("0"), I(digits + 1 - len));
            scaled_string = Texts(z, scaled_string);
            len = digits + 1;
        }
        Text_t whole = Text$slice(scaled_string, I(1), I(len - digits));
        Text_t fraction = Text$slice(scaled_string, I(len - digits + 1), I(-1));
        result = Texts(whole, ".", fraction);
    }
    if (Int$compare_value(scaled_int, I(0)) < 0) {
        result = Texts("-", result);
    }
    return result;
}

PUREFUNC
static int32_t Real$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    Int_t x_int = Real$compute(*(Real_t *)x, 100);
    Int_t y_int = Real$compute(*(Real_t *)y, 100);
    return Int$compare_value(x_int, y_int);
}

PUREFUNC
static bool Real$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    Int_t x_int = Real$compute(*(Real_t *)x, 100);
    Int_t y_int = Real$compute(*(Real_t *)y, 100);
    return Int$equal_value(x_int, y_int);
}

PUREFUNC
static uint64_t Real$hash(const void *x, const TypeInfo_t *info) {
    (void)x, (void)info;
    fail("Hashing is not implemented for Reals");
}

static Text_t Real$as_text(const void *x, bool color, const TypeInfo_t *info) {
    (void)info;
    if (x == NULL) return Text("Real");
    Text_t text = Real$value_as_text(*(Real_t *)x, 10);
    return color ? Texts("\x1b[35m", text, "\x1b[m") : text;
}

PUREFUNC
static bool Real$is_none(const void *x, const TypeInfo_t *info) {
    (void)info;
    return *(Real_t *)x == NULL;
}

static void Real$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info) {
    (void)obj, (void)out, (void)pointers, (void)info;
    fail("Serialization of Reals is not implemented");
}

static void Real$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *info) {
    (void)in, (void)obj, (void)pointers, (void)info;
    fail("Serialization of Reals is not implemented");
}

public
const TypeInfo_t Real$info = {
    .size = sizeof(Real_t),
    .align = __alignof__(Real_t),
    .metamethods =
        {
            .compare = Real$compare,
            .equal = Real$equal,
            .hash = Real$hash,
            .as_text = Real$as_text,
            .is_none = Real$is_none,
            .serialize = Real$serialize,
            .deserialize = Real$deserialize,
        },
};
