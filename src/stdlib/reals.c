#include <gc.h>
#include <gmp.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "bigint.h"
#include "datatypes.h"
#include "floats.h"
#include "optionals.h"
#include "reals.h"
#include "text.h"
#include "types.h"

struct ieee754_bits {
    bool negative : 1;
    uint64_t biased_exponent : 11;
    uint64_t fraction : 53;
};

public
Int_t Real$compute(Real_t r, int64_t precision) {
    if (r->exact) return Int$left_shifted(r->approximation, Int$from_int64(precision));

    if (r->approximation.small != 0 && precision <= r->approximation_bits) {
        return Int$right_shifted(r->approximation, Int$from_int64(r->approximation_bits - precision));
    }

    r->approximation = r->compute(r, precision);
    r->approximation_bits = precision;
    return r->approximation;
}

static int64_t most_significant_bit(Real_t r, int64_t prec) {
    if ((r->approximation.small | 0x1) == 0x1) {
        (void)Real$compute(r, prec);
    }

    if ((r->approximation.small & 0x1) == 0x1) {
        int64_t small = (r->approximation.small) >> 2;
        if (small < 0) small = -small;
        int64_t msb = (int64_t)__builtin_clzl((uint64_t)small);
        return msb + r->approximation_bits;
    } else {
        size_t msb = mpz_sizeinbase(r->approximation.big, 2);
        return (int64_t)msb + r->approximation_bits;
    }
}

static Int_t Real$compute_int(Real_t r, int64_t precision) {
    return Int$left_shifted(r->userdata.i, Int$from_int64(precision));
}

static Int_t Real$compute_double(Real_t r, int64_t precision) {
    union {
        double n;
        struct ieee754_bits bits;
    } data = {.n = r->userdata.n};
    if (data.bits.biased_exponent + precision > 2047) {
        int64_t double_shift = 2047 - data.bits.biased_exponent;
        data.bits.biased_exponent += double_shift;
        return Int$left_shifted(Int$from_float64(data.n, true), Int$from_int64(precision - double_shift));
    } else {
        data.bits.biased_exponent += precision;
        return Int$from_float64(data.n, true);
    }
}

public
OptionalReal_t Real$parse(Text_t text, Text_t *remainder) {
    Text_t decimal_onwards = EMPTY_TEXT;
    OptionalInt_t int_component = Int$parse(text, NONE_INT, &decimal_onwards);
    if (int_component.small == 0) int_component = I(0);
    Text_t fraction_text = EMPTY_TEXT;
    if (Text$starts_with(decimal_onwards, Text("."), &fraction_text)) {
        fraction_text = Text$replace(fraction_text, Text("_"), EMPTY_TEXT);
        OptionalInt_t fraction = Int$parse(fraction_text, NONE_INT, remainder);
        if (fraction.small == 0) return NONE_REAL;
        int64_t shift = fraction_text.length;
        Int_t scale = Int$power(I(10), I(shift));
        Int_t i = Int$plus(Int$times(int_component, scale), fraction);
        return Real$divided_by(Real$from_int(i), Real$from_int(scale));
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
    int64_t my_msd = most_significant_bit(x, -1080 /* slightly > exp. range */);
    if (my_msd == INT64_MIN) return 0.0;
    int64_t needed_prec = my_msd - 60;
    union {
        double d;
        uint64_t bits;
    } scaled_int = {.d = Float64$from_int(Real$compute(x, needed_prec), true)};
    bool may_underflow = (needed_prec < -1000);
    uint64_t exp_adj = may_underflow ? (uint64_t)(needed_prec + 96l) : (uint64_t)needed_prec;
    uint64_t orig_exp = (scaled_int.bits >> 52) & 0x7fful;
    if (((orig_exp + exp_adj) & ~0x7fful) != 0) {
        // overflow
        if (scaled_int.d < 0.0) {
            return (double)-INFINITY;
        } else {
            return (double)INFINITY;
        }
    }
    scaled_int.bits += exp_adj << 52;
    if (may_underflow) {
        double two48 = (double)(1L << 48);
        return scaled_int.d / two48 / two48;
    } else {
        return scaled_int.d;
    }

    return 0.0;
}

public
Real_t Real$from_int(Int_t i) {
    return new (struct Real_s, .compute = Real$compute_int, .userdata.i = i, .approximation = i, .exact = 1,
                .approximation_bits = 0);
}

static Int_t Real$compute_negative(Real_t r, int64_t precision) {
    Int_t x = Real$compute(&r->userdata.children[0], precision);
    return Int$negative(x);
}

public
Real_t Real$negative(Real_t x) { return new (struct Real_s, .compute = Real$compute_negative, .userdata.children = x); }

static Int_t Real$compute_plus(Real_t r, int64_t precision) {
    Int_t lhs = Real$compute(&r->userdata.children[0], precision + 1);
    Int_t rhs = Real$compute(&r->userdata.children[1], precision + 1);
    return Int$right_shifted(Int$plus(lhs, rhs), I(1));
}

public
Real_t Real$plus(Real_t x, Real_t y) {
    Real_t result =
        new (struct Real_s, .compute = Real$compute_plus, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])), );
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

static Int_t Real$compute_minus(Real_t r, int64_t precision) {
    Int_t lhs = Real$compute(&r->userdata.children[0], precision + 1);
    Int_t rhs = Real$compute(&r->userdata.children[1], precision + 1);
    return Int$right_shifted(Int$minus(lhs, rhs), I(1));
}

public
Real_t Real$minus(Real_t x, Real_t y) {
    Real_t result =
        new (struct Real_s, .compute = Real$compute_minus, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])), );
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

static Int_t Real$compute_times(Real_t r, int64_t precision) {
    Real_t lhs = &r->userdata.children[0];
    Real_t rhs = &r->userdata.children[1];

    int64_t half_prec = (precision >> 1) - 1;

    int64_t lhs_msb = most_significant_bit(lhs, half_prec);
    int64_t rhs_msb = most_significant_bit(rhs, half_prec);

    Real_t big, small;
    if (lhs_msb >= rhs_msb) big = lhs, small = rhs;
    else big = rhs, small = lhs;

    Int_t approx_small = Real$compute(big, precision - MAX(lhs_msb, rhs_msb) - 3);
    if (approx_small.small == 0x1) return I(0);

    Int_t approx_big = Real$compute(small, precision - MIN(lhs_msb, rhs_msb) - 3);
    if (approx_big.small == 0x1) return I(0);

    return Int$right_shifted(Int$times(approx_big, approx_small), Int$from_int64(lhs_msb + rhs_msb - precision));
}

public
Real_t Real$times(Real_t x, Real_t y) {
    Real_t result =
        new (struct Real_s, .compute = Real$compute_times, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])));
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

static Int_t Real$compute_inverse(Real_t r, int64_t precision) {
    Real_t op = &r->userdata.children[0];
    int64_t msd = most_significant_bit(op, 99999);
    int64_t inv_msd = 1 - msd;
    int64_t digits_needed = inv_msd - precision + 3;
    int64_t prec_needed = msd - digits_needed;
    int64_t log_scale_factor = -precision - prec_needed;
    if (log_scale_factor < 0) return I(0);
    Int_t dividend = Int$left_shifted(I(1), I(log_scale_factor));
    Int_t scaled_divisor = Real$compute(op, prec_needed);
    Int_t abs_scaled_divisor = Int$abs(scaled_divisor);
    Int_t adj_dividend = Int$plus(dividend, Int$right_shifted(abs_scaled_divisor, I(1)));
    // Adjustment so that final result is rounded.
    Int_t result = Int$divided_by(adj_dividend, abs_scaled_divisor);
    if (Int$compare_value(scaled_divisor, I(0)) < 0) {
        return Int$negative(result);
    } else {
        return result;
    }

    return r->approximation;
}

public
Real_t Real$inverse(Real_t x) { return new (struct Real_s, .compute = Real$compute_inverse, .userdata.children = x); }

public
Real_t Real$divided_by(Real_t x, Real_t y) { return Real$times(x, Real$inverse(y)); }

static Int_t Real$compute_sqrt(Real_t r, int64_t precision) {
    static const int64_t fp_prec = 50;
    static const int64_t fp_op_prec = 60;

    Real_t operand = r->userdata.children;

    int64_t max_prec_needed = 2 * precision - 1;
    int64_t msd = most_significant_bit(operand, max_prec_needed);
    if (msd <= max_prec_needed) return I(0);
    int64_t result_msd = msd / 2; // +- 1
    int64_t result_digits = result_msd - precision; // +- 2
    if (result_digits > fp_prec) {
        // Compute less precise approximation and use a Newton iter.
        int64_t appr_digits = result_digits / 2 + 6;
        // This should be conservative.  Is fewer enough?
        int64_t appr_prec = result_msd - appr_digits;
        Int_t last_appr = Real$compute(r, appr_prec);
        int64_t prod_prec = 2 * appr_prec;
        Int_t op_appr = Real$compute(operand, prod_prec);
        // Slightly fewer might be enough;
        // Compute (last_appr * last_appr + op_appr)/(last_appr/2)
        // while adjusting the scaling to make everything work
        Int_t prod_prec_scaled_numerator = Int$plus(Int$times(last_appr, last_appr), op_appr);
        Int_t scaled_numerator = Int$left_shifted(prod_prec_scaled_numerator, Int$from_int64(appr_prec - precision));
        Int_t shifted_result = Int$divided_by(scaled_numerator, last_appr);
        return Int$right_shifted(Int$plus(shifted_result, I(1)), I(1));
    } else {
        // Use a double precision floating point approximation.
        // Make sure all precisions are even
        int64_t op_prec = (msd - fp_op_prec) & ~1L;
        int64_t working_prec = op_prec - fp_op_prec;
        Int_t scaled_bi_appr = Int$left_shifted(Real$compute(operand, op_prec), Int$from_int64(fp_op_prec));
        double scaled_appr = Float64$from_int(scaled_bi_appr, true);
        if (scaled_appr < 0.0) fail("Underflow?!?!");
        double scaled_fp_sqrt = sqrt(scaled_appr);
        Int_t scaled_sqrt = Int$from_float64(scaled_fp_sqrt, true);
        int64_t shift_count = working_prec / 2 - precision;
        return Int$left_shifted(scaled_sqrt, Int$from_int64(shift_count));
    }

    return I(0);
}

public
Real_t Real$sqrt(Real_t x) { return new (struct Real_s, .compute = Real$compute_sqrt, .userdata.children = x); }

public
Text_t Real$value_as_text(Real_t x, int64_t digits) {
    Int_t scale_factor = Int$power(I(10), Int$from_int64(digits));
    Real_t scaled = Real$times(x, Real$from_int(scale_factor));
    Int_t scaled_int = Real$compute(scaled, 0);
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
