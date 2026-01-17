#include <fenv.h>
#include <gc.h>
#include <gmp.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bigint.h"
#include "datatypes.h"
#include "floats.h"
#include "reals.h"
#include "text.h"
#include "types.h"

// Check if num is boxed pointer
public
CONSTFUNC bool Real$is_boxed(Real_t n) {
    return (n.bits & QNAN_MASK) == 0;
}

public
CONSTFUNC uint64_t Real$tag(Real_t n) {
    return n.bits & TAG_MASK;
}

static inline Real_t R(double d) {
    union {
        volatile double d;
        volatile uint64_t bits;
    } input = {.d = d};
    return (Real_t){.bits = input.bits ^ QNAN_MASK};
}

static inline Real_t mpq_to_real(__mpq_struct mpq) {
    volatile rational_t *r = GC_MALLOC(sizeof(rational_t));
    r->value = mpq;
    return (Real_t){.rational = (void *)r + REAL_TAG_RATIONAL};
}

static inline Real_t _sym_to_real(symbolic_t sym) {
    volatile symbolic_t *s = GC_MALLOC(sizeof(symbolic_t));
    *s = sym;
    return Real$simplify((Real_t){.symbolic = (void *)s + REAL_TAG_SYMBOLIC});
}
#define sym_to_real(...) _sym_to_real((symbolic_t){__VA_ARGS__})

public
Real_t Real$from_int64(int64_t i) {
    double d = (double)i;
    if ((int64_t)d == i) return R(d);

    volatile Int_t *b = GC_MALLOC(sizeof(Int_t));
    *b = I(i);
    return (Real_t){.bigint = (void *)b + REAL_TAG_BIGINT};
}

public
Real_t Real$from_rational(int64_t num, int64_t den) {
    __mpq_struct ret;
    mpq_init(&ret);
    if (den == INT64_MIN) fail("Domain error");
    if (den < 0) {
        if (num == INT64_MIN) fail("Domain error");
        num = -num;
        den = -den;
    }
    if (den == 0) fail("Division by zero");
    mpq_set_si(&ret, num, (unsigned long)den);
    mpq_canonicalize(&ret);
    return mpq_to_real(ret);
}

static CONSTFUNC bool Real$is_symbolic(Real_t x) {
    return Real$is_boxed(x) && Real$tag(x) == REAL_TAG_SYMBOLIC;
}

static void extract_coef_base(Real_t x, Real_t *coef, Real_t *base) {
    if (Real$is_symbolic(x)) {
        symbolic_t *sym = REAL_SYMBOLIC(x);
        if (sym->op == SYM_MUL) {
            if (!Real$is_symbolic(sym->left)) {
                *coef = sym->left;
                *base = sym->right;
                return;
            } else if (!Real$is_symbolic(sym->right)) {
                *coef = sym->right;
                *base = sym->left;
                return;
            }
        }
    }
    *coef = R(1);
    *base = x;
}

public
Real_t Real$simplify(Real_t x) {
    if (!Real$is_boxed(x) || Real$tag(x) != REAL_TAG_SYMBOLIC) return x;

    symbolic_t *sym = REAL_SYMBOLIC(x);
    Real_t left = Real$simplify(sym->left);
    Real_t right = Real$simplify(sym->right);

    switch (sym->op) {
    case SYM_ADD: {
        if (Real$obviously_equal(left, R(0))) return right;
        if (Real$obviously_equal(right, R(0))) return left;
        if (Real$obviously_equal(left, right)) return Real$times(R(2), left);

        Real_t lc, lb, rc, rb;
        extract_coef_base(left, &lc, &lb);
        extract_coef_base(right, &rc, &rb);
        if (Real$obviously_equal(lb, rb)) {
            Real_t sum = Real$plus(lc, rc);
            if (Real$obviously_equal(sum, R(1))) return lb;
            return Real$times(sum, lb);
        }
        break;
    }
    case SYM_SUB: {
        if (Real$obviously_equal(left, right)) return R(0);
        if (Real$obviously_equal(right, R(0))) return left;

        Real_t lc, lb, rc, rb;
        extract_coef_base(left, &lc, &lb);
        extract_coef_base(right, &rc, &rb);
        if (Real$obviously_equal(lb, rb)) {
            Real_t sum = Real$minus(lc, rc);
            if (Real$obviously_equal(sum, R(1))) return lb;
            return Real$times(sum, lb);
        }
        break;
    }
    case SYM_MUL:
        if (Real$obviously_equal(left, R(0)) || Real$obviously_equal(right, R(0))) return R(0);
        if (Real$obviously_equal(left, R(1))) return right;
        if (Real$obviously_equal(right, R(1))) return left;
        break;
    case SYM_DIV:
        if (Real$obviously_equal(left, right)) return R(1);
        if (Real$obviously_equal(right, R(1))) return left;
        break;
    case SYM_POW:
        if (Real$obviously_equal(right, R(0))) return R(1);
        if (Real$obviously_equal(right, R(1))) return left;
        break;
    case SYM_SIN:
        if (Real$obviously_equal(left, R(0))) return R(0);
        break;
    case SYM_COS:
        if (Real$obviously_equal(left, R(0))) return R(1);
        break;
    case SYM_TAN:
        if (Real$obviously_equal(left, R(0))) return R(0);
        break;
    default: break;
    }

    if (left.bits == sym->left.bits && right.bits == sym->right.bits) return x;
    return sym_to_real(.op = sym->op, .left = left, .right = right);
}

public
bool Real$get_rational(Real_t x, int64_t *num, int64_t *den) {
    if (!Real$is_boxed(x) || Real$tag(x) != REAL_TAG_RATIONAL) {
        return false;
    }

    rational_t *r = REAL_RATIONAL(x);

    // Check if both numerator and denominator fit in int64_t
    if (!mpz_fits_slong_p(mpq_numref(&r->value)) || !mpz_fits_slong_p(mpq_denref(&r->value))) {
        return false;
    }

    if (num) *num = mpz_get_si(mpq_numref(&r->value));
    if (den) *den = mpz_get_si(mpq_denref(&r->value));

    return true;
}

// Promote double to exact type if needed
static inline Real_t Real$as_rational(double d) {
    if (!isfinite(d)) {
        return R(d);
    }
    __mpq_struct ret;
    mpq_init(&ret);
    mpq_set_d(&ret, d);
    return mpq_to_real(ret);
}

public
CONSTFUNC Real_t Real$from_float64(double n) {
    return R(n);
}

public
Real_t Real$from_int(Int_t i) {
    double d = Float64$from_int(i, true);
    if (Int$equal_value(i, Int$from_float64(d, true))) return R(d);

    volatile Int_t *b = GC_MALLOC(sizeof(Int_t));
    *b = i;
    return (Real_t){.bigint = (void *)b + REAL_TAG_BIGINT};
}

public
double Real$as_float64(Real_t n, bool truncate) {
    if (!Real$is_boxed(n)) {
        return REAL_DOUBLE(n);
    }

    switch (Real$tag(n)) {
    case REAL_TAG_BIGINT: {
        return Float64$from_int(*REAL_BIGINT(n), truncate);
    }
    case REAL_TAG_RATIONAL: {
        rational_t *rational = REAL_RATIONAL(n);
        return mpq_get_d(&rational->value);
    }
    case REAL_TAG_SYMBOLIC: {
        symbolic_t *s = REAL_SYMBOLIC(n);
        double left = Real$as_float64(s->left, truncate);
        double right = Real$as_float64(s->right, truncate);
        switch (s->op) {
        case SYM_INVALID: fail("Invalid number!");
        case SYM_ADD: return left + right;
        case SYM_SUB: return left - right;
        case SYM_MUL: return left * right;
        case SYM_DIV: return left / right;
        case SYM_MOD: return Float64$mod(left, right);
        case SYM_SQRT: return sqrt(left);
        case SYM_POW: return pow(left, right);
        case SYM_SIN: return sin(left);
        case SYM_COS: return cos(left);
        case SYM_TAN: return tan(left);
        case SYM_ASIN: return asin(left);
        case SYM_ACOS: return acos(left);
        case SYM_ATAN: return atan(left);
        case SYM_ATAN2: return atan2(left, right);
        case SYM_EXP: return exp(left);
        case SYM_LOG: return log(left);
        case SYM_LOG10: return log10(left);
        case SYM_ABS: return fabs(left);
        case SYM_FLOOR: return floor(left);
        case SYM_CEIL: return ceil(left);
        case SYM_PI: return M_PI;
        case SYM_TAU: return 2. * M_PI;
        case SYM_E: return M_E;
        default: return NAN;
        }
    }
    default: return NAN;
    }
}

symbolic_t pi_symbol = {.op = SYM_PI};
public
Real_t Real$pi = {.symbolic = (void *)&pi_symbol + REAL_TAG_SYMBOLIC};

symbolic_t tau_symbol = {.op = SYM_TAU};
public
Real_t Real$tau = {.symbolic = (void *)&tau_symbol + REAL_TAG_SYMBOLIC};

symbolic_t e_symbol = {.op = SYM_E};
public
Real_t Real$e = {.symbolic = (void *)&e_symbol + REAL_TAG_SYMBOLIC};

public
Real_t Real$plus(Real_t a, Real_t b) {
    if (Real$is_zero(a)) return b;
    else if (Real$is_zero(b)) return a;

    if (!Real$is_boxed(a) && !Real$is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        volatile double result = REAL_DOUBLE(a) + REAL_DOUBLE(b);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }

    if (!Real$is_boxed(a)) a = Real$as_rational(REAL_DOUBLE(a));
    if (!Real$is_boxed(b)) b = Real$as_rational(REAL_DOUBLE(b));

    // Handle exact rational arithmetic
    if (Real$tag(a) == REAL_TAG_RATIONAL && Real$tag(b) == REAL_TAG_RATIONAL) {
        __mpq_struct ret;
        mpq_init(&ret);
        mpq_add(&ret, &REAL_RATIONAL(a)->value, &REAL_RATIONAL(b)->value);
        return mpq_to_real(ret);
    }

    // Fallback: create symbolic expression
    return sym_to_real(.op = SYM_ADD, .left = a, .right = b);
}

public
Real_t Real$minus(Real_t a, Real_t b) {
    if (Real$is_zero(b)) return a;
    else if (Real$is_zero(a)) return Real$negative(b);
    else if (Real$obviously_equal(a, b)) return R(0);

    if (!Real$is_boxed(a) && !Real$is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        volatile double result = REAL_DOUBLE(a) - REAL_DOUBLE(b);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }

    if (!Real$is_boxed(a)) a = Real$as_rational(REAL_DOUBLE(a));
    if (!Real$is_boxed(b)) b = Real$as_rational(REAL_DOUBLE(b));

    if (Real$tag(a) == REAL_TAG_RATIONAL && Real$tag(b) == REAL_TAG_RATIONAL) {
        __mpq_struct ret;
        mpq_init(&ret);
        mpq_sub(&ret, &REAL_RATIONAL(a)->value, &REAL_RATIONAL(b)->value);
        return mpq_to_real(ret);
    }

    return sym_to_real(.op = SYM_SUB, .left = a, .right = b);
}

public
Real_t Real$times(Real_t a, Real_t b) {
    if (!Real$is_boxed(a) && REAL_DOUBLE(a) == 1.0) return b;
    if (!Real$is_boxed(b) && REAL_DOUBLE(b) == 1.0) return a;

    if (!Real$is_boxed(a) && !Real$is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        volatile double result = REAL_DOUBLE(a) * REAL_DOUBLE(b);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }

    if (!Real$is_boxed(a)) a = Real$as_rational(REAL_DOUBLE(a));
    if (!Real$is_boxed(b)) b = Real$as_rational(REAL_DOUBLE(b));

    if (Real$tag(a) == REAL_TAG_RATIONAL && Real$tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = REAL_RATIONAL(a);
        rational_t *rb = REAL_RATIONAL(b);
        __mpq_struct ret;
        mpq_init(&ret);
        mpq_mul(&ret, &ra->value, &rb->value);
        return mpq_to_real(ret);
    }

    // Check for sqrt(x) * sqrt(x) = x
    if (Real$tag(a) == REAL_TAG_SYMBOLIC && Real$tag(b) == REAL_TAG_SYMBOLIC) {
        symbolic_t *sa = REAL_SYMBOLIC(a);
        symbolic_t *sb = REAL_SYMBOLIC(b);
        if (sa->op == SYM_SQRT && sb->op == SYM_SQRT) {
            // Check if arguments are equal
            if (Real$equal_values(sa->left, sb->left)) {
                return sa->left;
            }
        }
    }

    // Prefer putting symbolic in RHS to make simplifying easier
    if (Real$tag(a) == REAL_TAG_SYMBOLIC && Real$tag(b) != REAL_TAG_SYMBOLIC)
        return sym_to_real(.op = SYM_MUL, .left = b, .right = a);

    return sym_to_real(.op = SYM_MUL, .left = a, .right = b);
}

public
Real_t Real$divided_by(Real_t a, Real_t b) {
    if (REAL_DOUBLE(b) == 1.0) return a;

    if (!Real$is_boxed(a) && !Real$is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        volatile double result = REAL_DOUBLE(a) / REAL_DOUBLE(b);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }

    if (!Real$is_boxed(a)) a = Real$as_rational(REAL_DOUBLE(a));
    if (!Real$is_boxed(b)) b = Real$as_rational(REAL_DOUBLE(b));

    if (Real$tag(a) == REAL_TAG_RATIONAL && Real$tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = REAL_RATIONAL(a);
        rational_t *rb = REAL_RATIONAL(b);
        __mpq_struct ret;
        mpq_init(&ret);
        mpq_div(&ret, &ra->value, &rb->value);
        return mpq_to_real(ret);
    }

    volatile symbolic_t *s = GC_MALLOC(sizeof(symbolic_t));
    s->op = SYM_DIV;
    s->left = a;
    s->right = b;
    return sym_to_real(.op = SYM_DIV, .left = a, .right = b);
}

public
Real_t Real$mod(Real_t n, Real_t modulus) {
    // Euclidean division, see:
    // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf

    // For fast path with doubles
    if (!Real$is_boxed(n) && !Real$is_boxed(modulus)) {
        double r = remainder(REAL_DOUBLE(n), REAL_DOUBLE(modulus));
        r -= (r < 0.0) * (2.0 * (REAL_DOUBLE(modulus) < 0.0) - 1.0) * REAL_DOUBLE(modulus);
        return R(r);
    }

    // For rationals, compute exactly
    if (Real$tag(n) == REAL_TAG_RATIONAL && Real$tag(modulus) == REAL_TAG_RATIONAL) {
        rational_t *rn = REAL_RATIONAL(n);
        rational_t *rm = REAL_RATIONAL(modulus);

        // Compute n - floor(n/modulus) * modulus
        mpq_t quotient, floored, result;
        mpq_init(quotient);
        mpq_init(floored);
        mpq_init(result);

        mpq_div(quotient, &rn->value, &rm->value);

        // Floor the quotient
        mpz_t floor_z;
        mpz_init(floor_z);
        mpz_fdiv_q(floor_z, mpq_numref(quotient), mpq_denref(quotient));
        mpq_set_z(floored, floor_z);

        // result = n - floor * modulus
        mpq_mul(result, floored, &rm->value);
        mpq_sub(result, &rn->value, result);

        // Apply Euclidean correction
        if (mpq_sgn(result) < 0) {
            if (mpq_sgn(&rm->value) < 0) {
                mpq_sub(result, result, &rm->value);
            } else {
                mpq_add(result, result, &rm->value);
            }
        }

        __mpq_struct ret;
        mpq_init(&ret);
        mpq_set(&ret, result);

        mpq_clear(quotient);
        mpq_clear(floored);
        mpq_clear(result);
        mpz_clear(floor_z);
        return mpq_to_real(ret);
    }

    // Fallback to symbolic
    return sym_to_real(.op = SYM_MOD, .left = n, .right = modulus);
}

public
Real_t Real$mod1(Real_t n, Real_t modulus) {
    Real_t one = R(1.0);
    return Real$plus(one, Real$mod(Real$minus(n, one), modulus));
}

public
Real_t Real$mix(Real_t amount, Real_t x, Real_t y) {
    // mix = (1 - amount) * x + amount * y
    Real_t one = R(1.0);
    Real_t one_minus_amount = Real$minus(one, amount);
    Real_t left_term = Real$times(one_minus_amount, x);
    Real_t right_term = Real$times(amount, y);
    return Real$plus(left_term, right_term);
}

public
bool Real$is_between(Real_t x, Real_t low, Real_t high) {
    return Real$compare_values(low, x) <= 0 && Real$compare_values(x, high) <= 0;
}

public
Real_t Real$clamped(Real_t x, Real_t low, Real_t high) {
    if (Real$compare_values(x, low) <= 0) return low;
    if (Real$compare_values(x, high) >= 0) return high;
    return x;
}

public
Real_t Real$sqrt(Real_t a) {
    if (!Real$is_boxed(a)) {
        feclearexcept(FE_INEXACT);
        volatile double d = sqrt(REAL_DOUBLE(a));
        volatile double check = d * d;
        if (!fetestexcept(FE_INEXACT) && check == REAL_DOUBLE(a)) {
            return R(d);
        }
    }

    // Check for perfect square in rationals
    if (Real$tag(a) == REAL_TAG_RATIONAL) {
        rational_t *r = REAL_RATIONAL(a);
        mpz_t num_sqrt, den_sqrt;
        mpz_init(num_sqrt);
        mpz_init(den_sqrt);

        if (mpz_perfect_square_p(mpq_numref(&r->value)) && mpz_perfect_square_p(mpq_denref(&r->value))) {
            mpz_sqrt(num_sqrt, mpq_numref(&r->value));
            mpz_sqrt(den_sqrt, mpq_denref(&r->value));

            __mpq_struct ret;
            mpq_init(&ret);
            mpq_set_num(&ret, num_sqrt);
            mpq_set_den(&ret, den_sqrt);
            mpq_canonicalize(&ret);

            mpz_clear(num_sqrt);
            mpz_clear(den_sqrt);
            return mpq_to_real(ret);
        }
        mpz_clear(num_sqrt);
        mpz_clear(den_sqrt);
    }

    return sym_to_real(.op = SYM_SQRT, .left = a, .right = R(0));
}

// Because the libm implementation of pow() is often inexact for common
// cases like `10^2` due to its implementation details, this wrapper will
// try to find cases where it's not too hard to get exact results and do
// that instead, falling back to inexact pow() when necessary.
static CONSTFUNC double less_inexact_pow(double base, double exponent) {
    if (exponent == 0.0) return 1.0;
    if (base == 0.0) return exponent > 0.0 ? 0.0 : HUGE_VAL;
    if (exponent == 1.0) return base;
    if (exponent == 0.5) return sqrt(base);

    // Integer or half-integer path (x^i or x^(i+.5), where i is an integer)
    double int_part = trunc(exponent);
    double frac_part = exponent - int_part;
    if ((frac_part == 0.0 || frac_part == 0.5) && fabs(int_part) <= INT64_MAX) {
        int64_t n = (int64_t)fabs(int_part);
        double result = 1.0, b = base;
        while (n) {
            if (n & 1) result *= b;
            b *= b;
            n >>= 1;
        }
        if (frac_part == 0.5) result *= sqrt(base);
        return int_part < 0.0 ? 1.0 / result : result;
    }
    return pow(base, exponent);
}

public
Real_t Real$power(Real_t base, Real_t exp) {
    if (!Real$is_boxed(base) && !Real$is_boxed(exp)) {
        feclearexcept(FE_INEXACT);
        volatile double result = less_inexact_pow(REAL_DOUBLE(base), REAL_DOUBLE(exp));
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }

    return sym_to_real(.op = SYM_POW, .left = base, .right = exp);
}

// Helper function for binary functions
static Text_t format_binary_func(Text_t left, Text_t right, const char *func_name, bool colorize) {
    const char *operator_color = colorize ? "\033[33m" : "";
    const char *paren_color = colorize ? "\033[37m" : "";
    const char *reset = colorize ? "\033[m" : "";

    return colorize ? Texts(operator_color, func_name, paren_color, "(", reset, left, operator_color, ", ", reset,
                            right, paren_color, ")", reset)
                    : Texts(func_name, "(", left, ", ", right, ")");
}

// Helper function for constants
static Text_t format_constant(const char *symbol, bool colorize) {
    const char *number_color = colorize ? "\033[35m" : "";
    const char *reset = colorize ? "\033[m" : "";

    return colorize ? Texts(number_color, symbol, reset) : Text$from_str(symbol);
}

public
Text_t Real$as_text(const void *n, bool colorize, const TypeInfo_t *type) {
    (void)type;
    if (n == NULL) return Text("Real");

    Real_t num = *(Real_t *)n;

    // ANSI color codes
    const char *number_color = colorize ? "\033[35m" : ""; // magenta for numbers
    const char *operator_color = colorize ? "\033[33m" : ""; // yellow for operators
    const char *reset = colorize ? "\033[m" : "";

    if (!Real$is_boxed(num)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", REAL_DOUBLE(num));
        return colorize ? Texts(number_color, buf, reset) : Text$from_str(buf);
    }

    switch (Real$tag(num)) {
    case REAL_TAG_BIGINT: {
        Int_t *b = REAL_BIGINT(num);
        Text_t int_text = Int$value_as_text(*b);
        return colorize ? Texts(number_color, int_text, reset) : int_text;
    }
    case REAL_TAG_RATIONAL: {
        rational_t *r = REAL_RATIONAL(num);

        // Check if denominator is 1 (integer)
        if (mpz_cmp_ui(mpq_denref(&r->value), 1) == 0) {
            char *str = mpz_get_str(NULL, 10, mpq_numref(&r->value));
            Text_t result = colorize ? Texts(number_color, str, reset) : Text$from_str(str);
            return result;
        }

        // Check if denominator is 2^a * 5^b (terminates in decimal)
        mpz_t den_copy;
        mpz_init_set(den_copy, mpq_denref(&r->value));

        while (mpz_divisible_ui_p(den_copy, 2)) {
            mpz_divexact_ui(den_copy, den_copy, 2);
        }
        while (mpz_divisible_ui_p(den_copy, 5)) {
            mpz_divexact_ui(den_copy, den_copy, 5);
        }

        bool is_terminating_decimal = (mpz_cmp_ui(den_copy, 1) == 0);
        mpz_clear(den_copy);

        if (is_terminating_decimal) {
            // Compute exact decimal representation
            mpz_t num_scaled, den_temp;
            mpz_init_set(num_scaled, mpq_numref(&r->value));
            mpz_init_set(den_temp, mpq_denref(&r->value));

            size_t decimal_places = 0;
            while (mpz_cmp_ui(den_temp, 1) != 0) {
                mpz_mul_ui(num_scaled, num_scaled, 10);
                if (mpz_divisible_ui_p(den_temp, 2)) mpz_divexact_ui(den_temp, den_temp, 2);
                else if (mpz_divisible_ui_p(den_temp, 5)) mpz_divexact_ui(den_temp, den_temp, 5);
                else break;
                decimal_places++;
            }

            mpz_divexact(num_scaled, num_scaled, mpq_denref(&r->value));

            char *num_str = mpz_get_str(NULL, 10, num_scaled);
            size_t len = strlen(num_str);
            bool is_negative = (num_str[0] == '-');
            size_t start = is_negative ? 1 : 0;

            char buf[256];
            if (len - start <= decimal_places) {
                int leading_zeros = decimal_places - (len - start);
                if (leading_zeros > 0) {
                    snprintf(buf, sizeof(buf), "%s0.%0*d%s", is_negative ? "-" : "", leading_zeros, 0, num_str + start);
                } else {
                    snprintf(buf, sizeof(buf), "%s0.%s", is_negative ? "-" : "", num_str + start);
                }
            } else {
                int int_len = len - start - decimal_places;
                snprintf(buf, sizeof(buf), "%.*s.%s", (int)start + int_len, num_str, num_str + start + int_len);
            }

            // Strip trailing zeros after decimal point
            size_t buflen = strlen(buf);
            if (strchr(buf, '.')) {
                while (buflen > 0 && buf[buflen - 1] == '0') {
                    buf[--buflen] = '\0';
                }
                if (buflen > 0 && buf[buflen - 1] == '.') {
                    buf[--buflen] = '\0';
                }
            }

            mpz_clear(num_scaled);
            mpz_clear(den_temp);

            return colorize ? Texts(number_color, buf, reset) : Text$from_str(buf);
        }

        // Not a decimal, show as fraction
        char *num_str = mpz_get_str(NULL, 10, mpq_numref(&r->value));
        char *den_str = mpz_get_str(NULL, 10, mpq_denref(&r->value));
        Text_t result = colorize ? Texts(number_color, num_str, operator_color, "/", number_color, den_str, reset)
                                 : Texts(num_str, "/", den_str);
        return result;
    }
    case REAL_TAG_SYMBOLIC: {
        symbolic_t *s = REAL_SYMBOLIC(num);
        const char *func = NULL;
        const char *binop = NULL;
        switch (s->op) {
        case SYM_INVALID: return Text("INVALID REAL NUMBER");
        case SYM_ADD: binop = " + "; break;
        case SYM_SUB: binop = " - "; break;
        case SYM_MUL: binop = "*"; break;
        case SYM_DIV: binop = "/"; break;
        case SYM_MOD: binop = " mod "; break;
        case SYM_SQRT: func = "sqrt"; break;
        case SYM_POW: binop = "^"; break;
        case SYM_SIN: func = "sin"; break;
        case SYM_COS: func = "cos"; break;
        case SYM_TAN: func = "tan"; break;
        case SYM_ASIN: func = "asin"; break;
        case SYM_ACOS: func = "acos"; break;
        case SYM_ATAN: func = "atan"; break;
        case SYM_ATAN2: {
            Text_t left = Real$as_text(&s->left, colorize, type);
            Text_t right = Real$as_text(&s->right, colorize, type);
            return format_binary_func(left, right, "atan2", colorize);
        }
        case SYM_EXP: func = "exp"; break;
        case SYM_LOG: func = "log"; break;
        case SYM_LOG10: func = "log10"; break;
        case SYM_ABS: func = "abs"; break;
        case SYM_FLOOR: func = "floor"; break;
        case SYM_CEIL: func = "ceil"; break;
        case SYM_PI: return format_constant("π", colorize);
        case SYM_TAU: return format_constant("τ", colorize);
        case SYM_E: return format_constant("e", colorize);
        default: {
            Text_t left = Real$as_text(&s->left, colorize, type);
            Text_t right = Real$as_text(&s->right, colorize, type);
            return format_binary_func(left, right, "???", colorize);
        }
        }

        const char *paren_color = colorize ? "\033[37m" : "";
        if (func) {
            Text_t arg = Real$as_text(&s->left, colorize, type);
            return colorize ? Texts(operator_color, func, paren_color, "(", reset, arg, paren_color, ")", reset)
                            : Texts(func, "(", arg, ")");
        } else {
            Text_t left = Real$as_text(&s->left, colorize, type);
            Text_t right = Real$as_text(&s->right, colorize, type);
            return colorize ? Texts(paren_color, "(", reset, left, operator_color, binop, reset, right, paren_color,
                                    ")", reset)
                            : Texts("(", left, binop, right, ")");
        }
    }
    default: return colorize ? Texts(operator_color, "NaN", reset) : Text("NaN");
    }
}

public
Text_t Real$value_as_text(Real_t n) {
    return Real$as_text(&n, false, &Real$info);
}

public
OptionalReal_t Real$parse(Text_t text, Text_t *remainder) {
    OptionalInt_t int_part = Int$parse(text, I(10), &text);
    if (int_part.small == 0) {
        if (Text$starts_with(text, Text("."), NULL)) {
            int_part = I(0);
        } else {
            if (remainder) *remainder = text;
            return NONE_REAL;
        }
    }

    Real_t ret = Real$from_int(int_part);

    Text_t after_decimal;
    if (Text$starts_with(text, Text("."), &after_decimal)) {
        text = after_decimal;
        // Count zeroes:
        TextIter_t state = NEW_TEXT_ITER_STATE(text);
        int64_t i = 0, digits = 0;
        for (; i < text.length; i++) {
            int32_t g = Text$get_grapheme_fast(&state, i);
            if ('0' <= g && g <= '9') digits += 1;
            else if (g == '_') continue;
            else break;
        }

        if (digits > 0) {
            // n = int_part + fractional_part / 10^digits
            OptionalInt_t fractional_part = Int$parse(text, I(10), &after_decimal);
            if (fractional_part.small != 0 && !Int$is_zero(fractional_part)) {
                Real_t frac = Real$from_int(fractional_part);
                Real_t pow10 = Real$power(R(10.), R((double)digits));
                Real_t excess = Real$divided_by(frac, pow10);
                ret = Int$is_negative(int_part) ? Real$minus(ret, excess) : Real$plus(ret, excess);
            }
        }
        text = after_decimal;
    }

    Text_t after_exp;
    if (Text$starts_with(text, Text("e"), &after_exp) || Text$starts_with(text, Text("E"), &after_exp)) {
        OptionalInt_t exponent = Int$parse(after_exp, I(10), &after_exp);
        if (exponent.small != 0) {
            // n *= 10^exp
            if (!Int$is_zero(exponent)) {
                if (Int$is_negative(exponent)) {
                    ret = Real$divided_by(ret, Real$power(R(10.), Real$from_int(Int$negative(exponent))));
                } else {
                    ret = Real$times(ret, Real$power(R(10.), Real$from_int(exponent)));
                }
            }
            text = after_exp;
        }
    }

    if (remainder) *remainder = text;
    else if (text.length > 0) {
        return NONE_REAL;
    }

    return ret;
}

public
PUREFUNC bool Real$is_none(const void *vn, const TypeInfo_t *type) {
    (void)type;
    Real_t n = *(Real_t *)vn;
    return n.bits == REAL_TAG_NONE;
}

// Equality check (may be undecidable for some symbolics)
public
PUREFUNC bool Real$obviously_equal(Real_t a, Real_t b) {
    if (a.bits == b.bits) return true;
    if (!Real$is_boxed(a) && !Real$is_boxed(b)) return REAL_DOUBLE(a) == REAL_DOUBLE(b);
    if (Real$tag(a) != Real$tag(b)) return false;

    switch (Real$tag(a)) {
    case REAL_TAG_BIGINT: {
        return Int$equal_value(*REAL_BIGINT(a), *REAL_BIGINT(b));
    }
    case REAL_TAG_RATIONAL: {
        rational_t *ra = REAL_RATIONAL(a);
        rational_t *rb = REAL_RATIONAL(b);
        return mpq_equal(&ra->value, &rb->value);
    }
    case REAL_TAG_SYMBOLIC: {
        symbolic_t *sa = REAL_SYMBOLIC(a);
        symbolic_t *sb = REAL_SYMBOLIC(b);
        if (sa->op != sb->op) return false;
        return Real$obviously_equal(sa->left, sb->left) && Real$obviously_equal(sa->right, sb->right);
    }
    default: return false;
    }
}

// Equality check (may be undecidable for some symbolics)
public
bool Real$equal_values(Real_t a, Real_t b) {
    if (Real$obviously_equal(a, b)) return true;
    if (!Real$is_boxed(a) && !Real$is_boxed(b)) return REAL_DOUBLE(a) == REAL_DOUBLE(b);

    if (!Real$is_boxed(a)) a = Real$as_rational(REAL_DOUBLE(a));
    if (!Real$is_boxed(b)) b = Real$as_rational(REAL_DOUBLE(b));

    if (Real$tag(a) == REAL_TAG_RATIONAL && Real$tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = REAL_RATIONAL(a);
        rational_t *rb = REAL_RATIONAL(b);
        return mpq_equal(&ra->value, &rb->value);
    }

    // Refine symbolics until separation or timeout
    double da = Real$as_float64(a, true);
    double db = Real$as_float64(b, true);
    return fabs(da - db) < 1e-15;
}

public
bool Real$equal(const void *va, const void *vb, const TypeInfo_t *t) {
    (void)t;
    Real_t a = *(Real_t *)va;
    Real_t b = *(Real_t *)vb;
    return Real$equal_values(a, b);
}

public
uint64_t Real$hash(const void *vr, const TypeInfo_t *type) {
    (void)type;
    Text_t text = Real$value_as_text(*(Real_t *)vr);
    return Text$hash(&text, &Text$info);
}

// Comparison: -1 (less), 0 (equal), 1 (greater)
public
int32_t Real$compare_values(Real_t a, Real_t b) {
    if (!Real$is_boxed(a) && !Real$is_boxed(b)) {
        return (REAL_DOUBLE(a) > REAL_DOUBLE(b)) - (REAL_DOUBLE(a) < REAL_DOUBLE(b));
    }

    if (Real$obviously_equal(a, b)) return 0;

    if (Real$tag(a) == REAL_TAG_RATIONAL && Real$tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = REAL_RATIONAL(a);
        rational_t *rb = REAL_RATIONAL(b);
        return mpq_cmp(&ra->value, &rb->value);
    }

    double da = Real$as_float64(a, true);
    double db = Real$as_float64(b, true);
    return (da > db) - (da < db);
}

public
int32_t Real$compare(const void *va, const void *vb, const TypeInfo_t *t) {
    (void)t;
    Real_t a = *(Real_t *)va;
    Real_t b = *(Real_t *)vb;
    return Real$compare_values(a, b);
}

// Unary negation
public
Real_t Real$negative(Real_t a) {
    if (!Real$is_boxed(a)) return R(-REAL_DOUBLE(a));

    if (Real$tag(a) == REAL_TAG_RATIONAL) {
        rational_t *rat = REAL_RATIONAL(a);
        __mpq_struct ret;
        mpq_init(&ret);
        mpq_neg(&ret, &rat->value);
        return mpq_to_real(ret);
    }

    return Real$times(R(-1.0), a);
}

public
Real_t Real$rounded_to(Real_t x, Real_t round_to) {
    // Convert to rationals for exact computation
    __mpq_struct rx;
    mpq_init(&rx);

    // Convert x to rational
    if (!Real$is_boxed(x)) {
        mpq_set_d(&rx, REAL_DOUBLE(x));
    } else if (Real$tag(x) == REAL_TAG_RATIONAL) {
        mpq_set(&rx, &REAL_RATIONAL(x)->value);
    } else {
        mpq_set_d(&rx, Real$as_float64(x, true));
    }

    __mpq_struct rr;
    mpq_init(&rr);
    // Convert round_to to rational
    if (!Real$is_boxed(round_to)) {
        mpq_set_d(&rr, REAL_DOUBLE(round_to));
    } else if (Real$tag(round_to) == REAL_TAG_RATIONAL) {
        mpq_set(&rr, &REAL_RATIONAL(round_to)->value);
    } else {
        mpq_set_d(&rr, Real$as_float64(round_to, true));
    }

    // Compute x / round_to
    mpq_t quotient;
    mpq_init(quotient);
    mpq_div(quotient, &rx, &rr);

    // Round to nearest integer using mpz_fdiv_qr for exact rounding
    mpz_t rounded, remainder;
    mpz_init(rounded);
    mpz_init(remainder);

    // Get 2 * numerator and denominator for rounding
    mpz_t doubled_num;
    mpz_init(doubled_num);
    mpz_mul_ui(doubled_num, mpq_numref(quotient), 2);

    // rounded = (2*num + den) / (2*den) using integer division
    mpz_add(doubled_num, doubled_num, mpq_denref(quotient));
    mpz_mul_ui(remainder, mpq_denref(quotient), 2);
    mpz_fdiv_q(rounded, doubled_num, remainder);

    // Multiply back: rounded * round_to
    __mpq_struct ret;
    mpq_init(&ret);
    mpq_set_z(&ret, rounded);
    mpq_mul(&ret, &ret, &rr);

    mpq_clear(&rx);
    mpq_clear(&rr);
    mpq_clear(quotient);
    mpz_clear(rounded);
    mpz_clear(remainder);
    mpz_clear(doubled_num);
    return mpq_to_real(ret);
}

// Trigonometric functions - return symbolic expressions
public
Real_t Real$sin(Real_t x) {
    if (!Real$is_boxed(x)) {
        double result = sin(REAL_DOUBLE(x));
        // Check if result is exact (e.g., sin(0) = 0)
        if (result == 0.0 || result == 1.0 || result == -1.0) {
            return R(result);
        }
    }
    return sym_to_real(.op = SYM_SIN, .left = x, .right = R(0));
}

public
Real_t Real$cos(Real_t x) {
    if (!Real$is_boxed(x)) {
        double result = cos(REAL_DOUBLE(x));
        if (result == 0.0 || result == 1.0 || result == -1.0) {
            return R(result);
        }
    }
    return sym_to_real(.op = SYM_COS, .left = x, .right = R(0));
}

public
Real_t Real$tan(Real_t x) {
    if (!Real$is_boxed(x)) {
        double result = tan(REAL_DOUBLE(x));
        if (result == 0.0) return R(0.0);
    }
    return sym_to_real(.op = SYM_TAN, .left = x, .right = R(0));
}

public
Real_t Real$asin(Real_t x) {
    if (!Real$is_boxed(x)) {
        double result = asin(REAL_DOUBLE(x));
        if (result == 0.0) return R(0.0);
    }
    return sym_to_real(.op = SYM_ASIN, .left = x, .right = R(0));
}

public
Real_t Real$acos(Real_t x) {
    if (!Real$is_boxed(x)) {
        double result = acos(REAL_DOUBLE(x));
        if (result == 0.0) return R(0.0);
    }
    return sym_to_real(.op = SYM_ACOS, .left = x, .right = R(0));
}

public
Real_t Real$atan(Real_t x) {
    if (!Real$is_boxed(x)) {
        double result = atan(REAL_DOUBLE(x));
        if (result == 0.0) return R(0.0);
    }
    return sym_to_real(.op = SYM_ATAN, .left = x, .right = R(0));
}

public
Real_t Real$atan2(Real_t y, Real_t x) {
    if (!Real$is_boxed(y) && !Real$is_boxed(x)) {
        double result = atan2(REAL_DOUBLE(y), REAL_DOUBLE(x));
        if (result == 0.0) return R(0.0);
    }
    return sym_to_real(.op = SYM_ATAN2, .left = y, .right = x);
}

public
Real_t Real$exp(Real_t x) {
    if (!Real$is_boxed(x)) {
        feclearexcept(FE_INEXACT);
        volatile double result = exp(REAL_DOUBLE(x));
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }
    return sym_to_real(.op = SYM_EXP, .left = x, .right = R(0));
}

public
Real_t Real$log(Real_t x) {
    if (!Real$is_boxed(x)) {
        feclearexcept(FE_INEXACT);
        volatile double result = log(REAL_DOUBLE(x));
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }
    return sym_to_real(.op = SYM_LOG, .left = x, .right = R(0));
}

public
Real_t Real$log10(Real_t x) {
    if (!Real$is_boxed(x)) {
        feclearexcept(FE_INEXACT);
        volatile double result = log10(REAL_DOUBLE(x));
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return R(result);
        }
    }
    return sym_to_real(.op = SYM_LOG10, .left = x, .right = R(0));
}

public
Real_t Real$abs(Real_t x) {
    if (!Real$is_boxed(x)) {
        return R(fabs(REAL_DOUBLE(x)));
    }

    if (Real$tag(x) == REAL_TAG_RATIONAL) {
        rational_t *r = REAL_RATIONAL(x);
        __mpq_struct ret;
        mpq_init(&ret);
        mpq_abs(&ret, &r->value);
        return mpq_to_real(ret);
    }
    return sym_to_real(.op = SYM_ABS, .left = x, .right = R(0));
}

public
Real_t Real$floor(Real_t x) {
    if (!Real$is_boxed(x)) {
        // TODO: this may be inexact in some rare cases
        return R(floor(REAL_DOUBLE(x)));
    }

    if (Real$tag(x) == REAL_TAG_RATIONAL) {
        rational_t *r = REAL_RATIONAL(x);
        mpz_t quotient;
        mpz_init(quotient);
        mpz_fdiv_q(quotient, mpq_numref(&r->value), mpq_denref(&r->value));

        __mpq_struct ret;
        mpq_init(&ret);
        mpq_set_z(&ret, quotient);
        mpz_clear(quotient);
        return mpq_to_real(ret);
    }

    return sym_to_real(.op = SYM_FLOOR, .left = x, .right = R(0));
}

public
Real_t Real$ceil(Real_t x) {
    if (!Real$is_boxed(x)) {
        return R(ceil(REAL_DOUBLE(x)));
    }

    if (Real$tag(x) == REAL_TAG_RATIONAL) {
        rational_t *r = REAL_RATIONAL(x);
        mpz_t quotient;
        mpz_init(quotient);
        mpz_cdiv_q(quotient, mpq_numref(&r->value), mpq_denref(&r->value));

        __mpq_struct ret;
        mpq_init(&ret);
        mpq_set_z(&ret, quotient);
        mpz_clear(quotient);
        return mpq_to_real(ret);
    }

    return sym_to_real(.op = SYM_CEIL, .left = x, .right = R(0));
}

public
int Real$test() {
    GC_INIT();

    printf("=== Exact Number System Tests ===\n\n");

    // Test 1: Simple double arithmetic (fast path)
    printf("Test 1: 2.0 + 3.0\n");
    Real_t a = R(2.0);
    Real_t b = R(3.0);
    Real_t result = Real$plus(a, b);
    Text_t s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 2: Exact rational arithmetic
    printf("Test 2: 1/3 + 1/6\n");
    Real_t third = Real$from_rational(1, 3);
    Real_t sixth = Real$from_rational(1, 6);
    result = Real$plus(third, sixth);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 1/2\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 3: (1/3) * 3 should give exactly 1
    printf("Test 3: (1/3) * 3\n");
    Real_t three = Real$from_rational(3, 1);
    result = Real$times(third, three);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 1\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 4: sqrt(4) is exact
    printf("Test 4: sqrt(4)\n");
    Real_t four = Real$from_rational(4, 1);
    result = Real$sqrt(four);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 2\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 5: sqrt(2) * sqrt(2) stays symbolic
    printf("Test 5: sqrt(2) * sqrt(2)\n");
    Real_t two = Real$from_rational(2, 1);
    Real_t sqrt2 = Real$sqrt(two);
    result = Real$times(sqrt2, sqrt2);
    s = Real$value_as_text(result);
    printf("Result (symbolic): %s\n", Text$as_c_string(s));
    printf("Approximate value: %.17g\n", Real$as_float64(result, true));
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 6: Complex symbolic expression
    printf("Test 6: (sqrt(2) + 1) * (sqrt(2) - 1)\n");
    Real_t one = R(1.0);
    Real_t sqrt2_plus_1 = Real$plus(sqrt2, one);
    Real_t sqrt2_minus_1 = Real$minus(sqrt2, one);
    result = Real$times(sqrt2_plus_1, sqrt2_minus_1);
    s = Real$value_as_text(result);
    printf("Result (symbolic): %s\n", Text$as_c_string(s));
    printf("Approximate value: %.17g\n", Real$as_float64(result, true));
    printf("Expected: 1 (but stays symbolic)\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 7: Division creating rational
    printf("Test 7: 5 / 7\n");
    Real_t five = Real$from_int64(5);
    Real_t seven = Real$from_int64(7);
    result = Real$divided_by(five, seven);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 5/7\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 8: Power that stays exact
    printf("Test 8: 2^3\n");
    result = Real$power(R(2.0), R(3.0));
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 9: Decimal arithmetic
    printf("Test 8: 2^3\n");
    result = Real$power(R(2.0), R(3.0));
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 9: Currency calculation that fails with doubles
    printf("Test 9: 0.1 + 0.2 (classic floating point error)\n");
    Real_t dime = Real$from_rational(1, 10);
    Real_t two_dime = Real$from_rational(2, 10);
    result = Real$plus(dime, two_dime);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Double arithmetic: %.17g\n", 0.1 + 0.2);
    printf("Expected: 0.3\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    // Test 10: Rounding
    printf("Test 10: round(sqrt(2), 0.00001)\n");
    result = Real$rounded_to(sqrt2, Real$from_rational(1, 100000));
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 1.41421\n");
    printf("Is boxed: %s\n\n", Real$is_boxed(result) ? "yes" : "no");

    printf("=== Tests Complete ===\n");

    return 0;
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
            // .serialize = Real$serialize,
            // .deserialize = Real$deserialize,
        },
};
