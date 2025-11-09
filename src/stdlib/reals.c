#include <gc.h>
#include <gmp.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "bigint.h"
#include "datatypes.h"
#include "nums.h"
#include "text.h"

struct ieee754_bits {
    bool negative : 1;
    uint64_t biased_exponent : 11;
    uint64_t fraction : 53;
};

Int_t Real$compute(Real_t r, int64_t precision) {
    if (r->exact) return Int$left_shifted(r->approximation, Int$from_int64(precision));

    if (r->approximation.small != 0 && precision <= r->approximation_bits)
        return Int$left_shifted(r->approximation, Int$from_int64(precision - r->approximation_bits));

    r->approximation = r->compute(r, precision);
    r->approximation_bits = precision;
    return r->approximation;
}

int64_t most_significant_bit(Real_t r, int64_t prec) {
    if ((r->approximation.small | 0x1) == 0x1) {
        (void)Real$compute(r, prec);
    }

    if (r->approximation.small == 1) {
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
        return Int$left_shifted(Int$from_num(data.n, true), Int$from_int64(precision - double_shift));
    } else {
        data.bits.biased_exponent += precision;
        return Int$from_num(data.n, true);
    }
}

Real_t Real$from_float64(double n) { return new (struct Real_s, .compute = Real$compute_double, .userdata.n = n); }

double Real$as_float64(Real_t x) {
    int64_t my_msd = most_significant_bit(x, -1080 /* slightly > exp. range */);
    if (my_msd == INT64_MIN) return 0.0;
    int needed_prec = my_msd - 60;
    double scaled_int = Num$from_int(Real$compute(x, needed_prec), true);
    bool may_underflow = (needed_prec < -1000);
    int64_t scaled_int_rep = *(int64_t *)&scaled_int;
    long exp_adj = may_underflow ? needed_prec + 96 : needed_prec;
    long orig_exp = (scaled_int_rep >> 52) & 0x7ff;
    if (((orig_exp + exp_adj) & ~0x7ff) != 0) {
        // overflow
        if (scaled_int < 0.0) {
            return -INFINITY;
        } else {
            return INFINITY;
        }
    }
    scaled_int_rep += exp_adj << 52;
    double result = *(double *)&scaled_int_rep;
    if (may_underflow) {
        double two48 = (double)(1L << 48);
        return result / two48 / two48;
    } else {
        return result;
    }

    return 0.0;
}

Real_t Real$from_int(Int_t i) {
    return new (struct Real_s, .compute = Real$compute_int, .userdata.i = i, .approximation = i, .exact = 1,
                .approximation_bits = 0);
}

static Int_t Real$compute_negative(Real_t r, int64_t precision) {
    Int_t x = Real$compute(&r->userdata.children[0], precision);
    return Int$negative(x);
}

Real_t Real$negative(Real_t x) { return new (struct Real_s, .compute = Real$compute_negative, .userdata.children = x); }

static Int_t Real$compute_plus(Real_t r, int64_t precision) {
    Int_t lhs = Real$compute(&r->userdata.children[0], precision + 1);
    Int_t rhs = Real$compute(&r->userdata.children[1], precision + 1);
    return Int$right_shifted(Int$plus(lhs, rhs), I(1));
}

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

    Int_t approx_big = Real$compute(big, precision - MIN(lhs_msb, rhs_msb) - 3);
    if (approx_big.small == 0x1) return I(0);

    return Int$right_shifted(Int$times(approx_big, approx_small), Int$from_int64(lhs_msb + rhs_msb - precision));
}

Real_t Real$times(Real_t x, Real_t y) {
    Real_t result =
        new (struct Real_s, .compute = Real$compute_times, .userdata.children = GC_MALLOC(sizeof(struct Real_s[2])));
    result->userdata.children[0] = *x;
    result->userdata.children[1] = *y;
    return result;
}

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
        double scaled_appr = Num$from_int(scaled_bi_appr, true);
        if (scaled_appr < 0.0) fail("Underflow?!?!");
        double scaled_fp_sqrt = sqrt(scaled_appr);
        Int_t scaled_sqrt = Int$from_num(scaled_fp_sqrt, true);
        int64_t shift_count = working_prec / 2 - precision;
        return Int$left_shifted(scaled_sqrt, Int$from_int64(shift_count));
    }

    return I(0);
}

Real_t Real$sqrt(Real_t x) { return new (struct Real_s, .compute = Real$compute_sqrt, .userdata.children = x); }

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
