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
#include "optionals.h"
#include "reals.h"
#include "text.h"
#include "types.h"

typedef struct {
    __mpq_struct value;
} rational_t;

typedef struct {
    double (*compute)(void *ctx, int precision);
    void *context;
} constructive_t;

typedef enum {
    SYM_ADD,
    SYM_SUB,
    SYM_MUL,
    SYM_DIV,
    SYM_MOD,
    SYM_SQRT,
    SYM_POW,
    SYM_SIN,
    SYM_COS,
    SYM_TAN,
    SYM_ASIN,
    SYM_ACOS,
    SYM_ATAN,
    SYM_ATAN2,
    SYM_EXP,
    SYM_LOG,
    SYM_LOG10,
    SYM_ABS,
    SYM_FLOOR,
    SYM_CEIL,
    SYM_PI,
    SYM_E
} sym_op_t;

typedef struct symbolic {
    sym_op_t op;
    Real_t left;
    Real_t right;
} symbolic_t;

// Check if num is boxed pointer
static inline bool is_boxed(Real_t n) {
    return (n.u64 & QNAN_MASK) == QNAN_MASK;
}

static inline uint64_t get_tag(Real_t n) {
    return n.u64 & TAG_MASK;
}

static inline void *get_ptr(Real_t n) {
    return (void *)(uintptr_t)(n.u64 & PTR_MASK);
}

static inline Real_t box_ptr(void *ptr, uint64_t tag) {
    Real_t n;
    n.u64 = QNAN_MASK | tag | ((uint64_t)(uintptr_t)ptr & PTR_MASK);
    return n;
}

static inline Real_t make_double(double d) {
    return (Real_t){.d = d};
}

public
Real_t Real$from_int64(int64_t i) {
    double d = (double)i;
    if ((int64_t)d == i) return make_double(d);

    Int_t *b = GC_MALLOC(sizeof(Int_t));
    *b = I(i);
    return box_ptr(b, REAL_TAG_BIGINT);
}

public
Real_t Real$from_rational(int64_t num, int64_t den) {
    rational_t *r = GC_MALLOC(sizeof(rational_t));
    mpq_init(&r->value);
    if (den == INT64_MIN) fail("Domain error");
    if (den < 0) {
        if (num == INT64_MIN) fail("Domain error");
        num = -num;
        den = -den;
    }
    if (den == 0) fail("Division by zero");
    mpq_set_si(&r->value, num, (unsigned long)den);
    mpq_canonicalize(&r->value);
    return box_ptr(r, REAL_TAG_RATIONAL);
}

// Promote double to exact type if needed
static Real_t promote_double(double d) {
    if (isfinite(d) && d == floor(d) && fabs(d) < (1LL << 53)) {
        return make_double(d);
    }
    rational_t *r = GC_MALLOC(sizeof(rational_t));
    mpq_init(&r->value);
    mpq_set_d(&r->value, d);
    return box_ptr(r, REAL_TAG_RATIONAL);
}

public
CONSTFUNC Real_t Real$from_float64(double n) {
    return make_double(n);  // Preserve sign of zero
}

public
Real_t Real$from_int(Int_t i) {
    double d = Float64$from_int(i, true);
    if (Int$equal_value(i, Int$from_float64(d, true)))
        return make_double(d);

    Int_t *b = GC_MALLOC(sizeof(Int_t));
    *b = i;
    return box_ptr(b, REAL_TAG_BIGINT);
}

public
double Real$as_float64(Real_t n, bool truncate) {
    if (!is_boxed(n)) return n.d;

    switch (get_tag(n)) {
    case REAL_TAG_BIGINT: {
        Int_t *b = get_ptr(n);
        return Float64$from_int(*b, truncate);
    }
    case REAL_TAG_RATIONAL: {
        rational_t *r = get_ptr(n);
        return mpq_get_d(&r->value);
    }
    case REAL_TAG_CONSTRUCTIVE: {
        constructive_t *c = get_ptr(n);
        return c->compute(c->context, 53);
    }
    case REAL_TAG_SYMBOLIC: {
        symbolic_t *s = get_ptr(n);
        double left = Real$as_float64(s->left, truncate);
        double right = Real$as_float64(s->right, truncate);
        switch (s->op) {
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
        case SYM_E: return M_E;
        default: return NAN;
        }
    }
    default: return NAN;
    }
}

public
Real_t Real$plus(Real_t a, Real_t b) {
    if (!is_boxed(a) && !is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        double result = a.d + b.d;
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
        a = promote_double(a.d);
        b = promote_double(b.d);
    }

    // Handle exact rational arithmetic
    if (get_tag(a) == REAL_TAG_RATIONAL && get_tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = get_ptr(a);
        rational_t *rb = get_ptr(b);
        rational_t *result = GC_MALLOC(sizeof(rational_t));
        mpq_init(&result->value);
        mpq_add(&result->value, &ra->value, &rb->value);
        return box_ptr(result, REAL_TAG_RATIONAL);
    }

    // Fallback: create symbolic expression
    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_ADD;
    sym->left = a;
    sym->right = b;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$minus(Real_t a, Real_t b) {
    if (!is_boxed(a) && !is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        double result = a.d - b.d;
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
        a = promote_double(a.d);
        b = promote_double(b.d);
    }

    if (get_tag(a) == REAL_TAG_RATIONAL && get_tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = get_ptr(a);
        rational_t *rb = get_ptr(b);
        rational_t *result = GC_MALLOC(sizeof(rational_t));
        mpq_init(&result->value);
        mpq_sub(&result->value, &ra->value, &rb->value);
        return box_ptr(result, REAL_TAG_RATIONAL);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_SUB;
    sym->left = a;
    sym->right = b;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$times(Real_t a, Real_t b) {
    if (!is_boxed(a) && !is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        double result = a.d * b.d;
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
        a = promote_double(a.d);
        b = promote_double(b.d);
    }

    if (get_tag(a) == REAL_TAG_RATIONAL && get_tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = get_ptr(a);
        rational_t *rb = get_ptr(b);
        rational_t *result = GC_MALLOC(sizeof(rational_t));
        mpq_init(&result->value);
        mpq_mul(&result->value, &ra->value, &rb->value);
        return box_ptr(result, REAL_TAG_RATIONAL);
    }

    // Check for sqrt(x) * sqrt(x) = x
    if (get_tag(a) == REAL_TAG_SYMBOLIC && get_tag(b) == REAL_TAG_SYMBOLIC) {
        symbolic_t *sa = get_ptr(a);
        symbolic_t *sb = get_ptr(b);
        if (sa->op == SYM_SQRT && sb->op == SYM_SQRT) {
            // Compare if same argument (simple pointer equality check)
            if (sa->left.u64 == sb->left.u64) {
                return sa->left;
            }
            // Also check if arguments are equal values (not just pointers)
            if (Real$equal(&sa->left, &sb->left, NULL)) {
                return sa->left;
            }
        }
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_MUL;
    sym->left = a;
    sym->right = b;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$divided_by(Real_t a, Real_t b) {
    if (!is_boxed(a) && !is_boxed(b)) {
        feclearexcept(FE_INEXACT);
        double result = a.d / b.d;
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
        a = promote_double(a.d);
        b = promote_double(b.d);
    }

    if (get_tag(a) == REAL_TAG_RATIONAL && get_tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = get_ptr(a);
        rational_t *rb = get_ptr(b);
        rational_t *result = GC_MALLOC(sizeof(rational_t));
        mpq_init(&result->value);
        mpq_div(&result->value, &ra->value, &rb->value);
        return box_ptr(result, REAL_TAG_RATIONAL);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_DIV;
    sym->left = a;
    sym->right = b;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$mod(Real_t n, Real_t modulus) {
    // Euclidean division, see:
    // https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf

    // For fast path with doubles
    if (!is_boxed(n) && !is_boxed(modulus)) {
        double r = remainder(n.d, modulus.d);
        r -= (r < 0.0) * (2.0 * (modulus.d < 0.0) - 1.0) * modulus.d;
        return make_double(r);
    }

    // For rationals, compute exactly
    if (get_tag(n) == REAL_TAG_RATIONAL && get_tag(modulus) == REAL_TAG_RATIONAL) {
        rational_t *rn = get_ptr(n);
        rational_t *rm = get_ptr(modulus);

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

        rational_t *res = GC_MALLOC(sizeof(rational_t));
        mpq_init(&res->value);
        mpq_set(&res->value, result);

        mpq_clear(quotient);
        mpq_clear(floored);
        mpq_clear(result);
        mpz_clear(floor_z);

        return box_ptr(res, REAL_TAG_RATIONAL);
    }

    // Fallback to symbolic
    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_MOD;
    sym->left = n;
    sym->right = modulus;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$mod1(Real_t n, Real_t modulus) {
    Real_t one = make_double(1.0);
    return Real$plus(one, Real$mod(Real$minus(n, one), modulus));
}

public
Real_t Real$mix(Real_t amount, Real_t x, Real_t y) {
    // mix = (1 - amount) * x + amount * y
    Real_t one = make_double(1.0);
    Real_t one_minus_amount = Real$minus(one, amount);
    Real_t left_term = Real$times(one_minus_amount, x);
    Real_t right_term = Real$times(amount, y);
    return Real$plus(left_term, right_term);
}

public
bool Real$is_between(Real_t x, Real_t low, Real_t high) {
    return Real$compare(&low, &x, NULL) <= 0 && Real$compare(&x, &high, NULL) <= 0;
}

public
Real_t Real$clamped(Real_t x, Real_t low, Real_t high) {
    if (Real$compare(&x, &low, NULL) <= 0) return low;
    if (Real$compare(&x, &high, NULL) >= 0) return high;
    return x;
}

public
Real_t Real$sqrt(Real_t a) {
    if (!is_boxed(a)) {
        double d = sqrt(a.d);
        feclearexcept(FE_INEXACT);
        double check = d * d;
        if (!fetestexcept(FE_INEXACT) && check == a.d) {
            return make_double(d);
        }
    }

    // Check for perfect square in rationals
    if (get_tag(a) == REAL_TAG_RATIONAL) {
        rational_t *r = get_ptr(a);
        mpz_t num_sqrt, den_sqrt;
        mpz_init(num_sqrt);
        mpz_init(den_sqrt);

        if (mpz_perfect_square_p(mpq_numref(&r->value)) && mpz_perfect_square_p(mpq_denref(&r->value))) {
            mpz_sqrt(num_sqrt, mpq_numref(&r->value));
            mpz_sqrt(den_sqrt, mpq_denref(&r->value));

            rational_t *result = GC_MALLOC(sizeof(rational_t));
            mpq_init(&result->value);
            mpq_set_num(&result->value, num_sqrt);
            mpq_set_den(&result->value, den_sqrt);
            mpq_canonicalize(&result->value);

            mpz_clear(num_sqrt);
            mpz_clear(den_sqrt);
            return box_ptr(result, REAL_TAG_RATIONAL);
        }
        mpz_clear(num_sqrt);
        mpz_clear(den_sqrt);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_SQRT;
    sym->left = a;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$power(Real_t base, Real_t exp) {
    if (!is_boxed(base) && !is_boxed(exp)) {
        feclearexcept(FE_INEXACT);
        double result = pow(base.d, exp.d);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_POW;
    sym->left = base;
    sym->right = exp;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

// Helper function for binary operations in parentheses
static Text_t format_binary_op(Text_t left, Text_t right, const char *op, bool colorize) {
    const char *operator_color = colorize ? "\033[33m" : "";
    const char *paren_color = colorize ? "\033[37m" : "";
    const char *reset = colorize ? "\033[m" : "";

    return colorize ? Texts(paren_color, "(", reset, left, operator_color, op, reset, right, paren_color, ")", reset)
                    : Texts("(", left, op, right, ")");
}

// Helper function for unary functions
static Text_t format_unary_func(Text_t arg, const char *func_name, bool colorize) {
    const char *operator_color = colorize ? "\033[33m" : "";
    const char *paren_color = colorize ? "\033[37m" : "";
    const char *reset = colorize ? "\033[m" : "";

    return colorize ? Texts(operator_color, func_name, paren_color, "(", reset, arg, paren_color, ")", reset)
                    : Texts(func_name, "(", arg, ")");
}

// Helper function for binary functions
static Text_t format_binary_func(Text_t left, Text_t right, const char *func_name, bool colorize) {
    const char *operator_color = colorize ? "\033[33m" : "";
    const char *paren_color = colorize ? "\033[37m" : "";
    const char *reset = colorize ? "\033[m" : "";

    return colorize ? Texts(operator_color, func_name, paren_color, "(", reset, left, operator_color, ", ", reset, right, paren_color, ")", reset)
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

    if (!is_boxed(num)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", num.d);
        return colorize ? Texts(number_color, buf, reset) : Text$from_str(buf);
    }

    switch (get_tag(num)) {
    case REAL_TAG_BIGINT: {
        Int_t *b = get_ptr(num);
        Text_t int_text = Int$value_as_text(*b);
        return colorize ? Texts(number_color, int_text, reset) : int_text;
    }
    case REAL_TAG_RATIONAL: {
        rational_t *r = get_ptr(num);

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
    case REAL_TAG_CONSTRUCTIVE: {
        constructive_t *c = get_ptr(num);
        double approx = c->compute(c->context, 53);
        char buf[64];
        snprintf(buf, sizeof(buf), "~%.17g", approx);
        return colorize ? Texts(operator_color, "~", number_color, buf + 1, reset) : Text$from_str(buf);
    }
    case REAL_TAG_SYMBOLIC: {
        symbolic_t *s = get_ptr(num);
        Text_t left = Real$as_text(&s->left, colorize, type);
        Text_t right = Real$as_text(&s->right, colorize, type);

        switch (s->op) {
        case SYM_ADD: return format_binary_op(left, right, " + ", colorize);
        case SYM_SUB: return format_binary_op(left, right, " - ", colorize);
        case SYM_MUL: return format_binary_op(left, right, " * ", colorize);
        case SYM_DIV: return format_binary_op(left, right, " / ", colorize);
        case SYM_MOD: return format_binary_op(left, right, " % ", colorize);
        case SYM_SQRT: return format_unary_func(left, "sqrt", colorize);
        case SYM_POW: return colorize ? Texts(left, operator_color, "^", reset, right) : Texts(left, "^", right);
        case SYM_SIN: return format_unary_func(left, "sin", colorize);
        case SYM_COS: return format_unary_func(left, "cos", colorize);
        case SYM_TAN: return format_unary_func(left, "tan", colorize);
        case SYM_ASIN: return format_unary_func(left, "asin", colorize);
        case SYM_ACOS: return format_unary_func(left, "acos", colorize);
        case SYM_ATAN: return format_unary_func(left, "atan", colorize);
        case SYM_ATAN2: return format_binary_func(left, right, "atan2", colorize);
        case SYM_EXP: return format_unary_func(left, "exp", colorize);
        case SYM_LOG: return format_unary_func(left, "log", colorize);
        case SYM_LOG10: return format_unary_func(left, "log10", colorize);
        case SYM_ABS: return format_unary_func(left, "abs", colorize);
        case SYM_FLOOR: return format_unary_func(left, "floor", colorize);
        case SYM_CEIL: return format_unary_func(left, "ceil", colorize);
        case SYM_PI: return format_constant("π", colorize);
        case SYM_E: return format_constant("e", colorize);
        default: return format_binary_op(left, right, " ? ", colorize);
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
    const char *str = Text$as_c_string(text);
    // Handle empty or null
    if (!str || !*str) return NONE_REAL;

    const char *p = str;
    const char *start = p;

    // Skip leading whitespace
    while (*p == ' ' || *p == '\t')
        p++;

    // Skip optional sign
    if (*p == '-' || *p == '+') p++;

    // Must have at least one digit
    if (!(*p >= '0' && *p <= '9')) return NONE_REAL;

    // Scan digits before decimal point
    while (*p >= '0' && *p <= '9')
        p++;

    // Check for decimal point
    bool has_dot = (*p == '.');
    if (has_dot) {
        p++;
        // Scan digits after decimal point
        while (*p >= '0' && *p <= '9')
            p++;
    }

    // Check for exponent (not supported yet, but detect it)
    bool has_exp = (*p == 'e' || *p == 'E');
    if (has_exp) return NONE_REAL; // Don't support scientific notation yet

    // Now p points to first non-digit character
    // Extract the valid number portion
    ptrdiff_t num_len = p - start;
    char buf[256];
    if (num_len >= 256) return NONE_REAL;
    strncpy(buf, start, (size_t)num_len);
    buf[num_len] = '\0';

    // If there's remaining text and no remainder pointer, fail
    if (*p != '\0' && remainder == NULL) return NONE_REAL;

    // Set remainder if provided
    if (remainder) {
        *remainder = Text$from_str(p);
    }

    // Now parse buf as number
    if (!has_dot) {
        // Integer
        char *endptr;
        long long val = strtoll(buf, &endptr, 10);
        if (endptr == buf + num_len) {
            return Real$from_int64(val);
        }

        // Too large for int64, use GMP
        OptionalInt_t b = Int$parse(Text$from_str(buf), NONE_INT, NULL);
        if (b.small == 0) return NONE_REAL;
        Int_t *bi = GC_MALLOC(sizeof(Int_t));
        *bi = b;
        return box_ptr(bi, REAL_TAG_BIGINT);
    }

    // Decimal - convert to rational
    rational_t *r = GC_MALLOC(sizeof(rational_t));
    mpq_init(&r->value);

    // Count decimal places
    const char *dot_pos = strchr(buf, '.');
    int decimal_places = 0;
    if (dot_pos) {
        const char *d = dot_pos + 1;
        while (*d >= '0' && *d <= '9') {
            decimal_places++;
            d++;
        }
    }

    // Remove decimal point
    char int_buf[256];
    int j = 0;
    for (int i = 0; buf[i] && j < 255; i++) {
        if (buf[i] != '.') {
            int_buf[j++] = buf[i];
        }
    }
    int_buf[j] = '\0';

    // Set numerator
    if (mpz_set_str(mpq_numref(&r->value), int_buf, 10) != 0) {
        mpq_clear(&r->value);
        return NONE_REAL;
    }

    // Set denominator = 10^decimal_places
    mpz_set_ui(mpq_denref(&r->value), 1);
    for (int i = 0; i < decimal_places; i++) {
        mpz_mul_ui(mpq_denref(&r->value), mpq_denref(&r->value), 10);
    }

    mpq_canonicalize(&r->value);
    return box_ptr(r, REAL_TAG_RATIONAL);
}

public
PUREFUNC bool Real$is_none(const void *vn, const TypeInfo_t *type) {
    (void)type;
    Real_t n = *(Real_t *)vn;
    return is_boxed(n) && get_tag(n) == REAL_TAG_NONE;
}

// Equality check (may be undecidable for some symbolics)
public
bool Real$equal(const void *va, const void *vb, const TypeInfo_t *t) {
    (void)t;
    Real_t a = *(Real_t *)va;
    Real_t b = *(Real_t *)vb;
    if (!is_boxed(a) && !is_boxed(b)) return a.d == b.d;
    if (is_boxed(a) != is_boxed(b)) return 0;

    if (get_tag(a) == REAL_TAG_RATIONAL && get_tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = get_ptr(a);
        rational_t *rb = get_ptr(b);
        return mpq_equal(&ra->value, &rb->value);
    }

    // Refine symbolics until separation or timeout
    double da = Real$as_float64(a, true);
    double db = Real$as_float64(b, true);
    return fabs(da - db) < 1e-15;
}

public
uint64_t Real$hash(const void *vr, const TypeInfo_t *type) {
    (void)type;
    Text_t text = Real$value_as_text(*(Real_t *)vr);
    return Text$hash(&text, &Text$info);
}

// Comparison: -1 (less), 0 (equal), 1 (greater)
public
int32_t Real$compare(const void *va, const void *vb, const TypeInfo_t *t) {
    (void)t;
    Real_t a = *(Real_t *)va;
    Real_t b = *(Real_t *)vb;
    if (!is_boxed(a) && !is_boxed(b)) {
        return (a.d > b.d) - (a.d < b.d);
    }

    if (get_tag(a) == REAL_TAG_RATIONAL && get_tag(b) == REAL_TAG_RATIONAL) {
        rational_t *ra = get_ptr(a);
        rational_t *rb = get_ptr(b);
        return mpq_cmp(&ra->value, &rb->value);
    }

    double da = Real$as_float64(a, true);
    double db = Real$as_float64(b, true);
    return (da > db) - (da < db);
}

// Unary negation
public
Real_t Real$negative(Real_t a) {
    if (!is_boxed(a)) return make_double(-a.d);

    if (get_tag(a) == REAL_TAG_RATIONAL) {
        rational_t *r = get_ptr(a);
        rational_t *result = GC_MALLOC(sizeof(rational_t));
        mpq_init(&result->value);
        mpq_neg(&result->value, &r->value);
        return box_ptr(result, REAL_TAG_RATIONAL);
    }

    return Real$times(make_double(-1.0), a);
}

public
Real_t Real$rounded_to(Real_t x, Real_t round_to) {
    // Convert to rationals for exact computation
    rational_t *rx = GC_MALLOC(sizeof(rational_t));
    rational_t *rr = GC_MALLOC(sizeof(rational_t));
    mpq_init(&rx->value);
    mpq_init(&rr->value);

    // Convert x to rational
    if (!is_boxed(x)) {
        mpq_set_d(&rx->value, x.d);
    } else if (get_tag(x) == REAL_TAG_RATIONAL) {
        mpq_set(&rx->value, &((rational_t *)get_ptr(x))->value);
    } else {
        mpq_set_d(&rx->value, Real$as_float64(x, true));
    }

    // Convert round_to to rational
    if (!is_boxed(round_to)) {
        mpq_set_d(&rr->value, round_to.d);
    } else if (get_tag(round_to) == REAL_TAG_RATIONAL) {
        mpq_set(&rr->value, &((rational_t *)get_ptr(round_to))->value);
    } else {
        mpq_set_d(&rr->value, Real$as_float64(round_to, true));
    }

    // Compute x / round_to
    mpq_t quotient;
    mpq_init(quotient);
    mpq_div(quotient, &rx->value, &rr->value);

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
    rational_t *result = GC_MALLOC(sizeof(rational_t));
    mpq_init(&result->value);
    mpq_set_z(&result->value, rounded);
    mpq_mul(&result->value, &result->value, &rr->value);

    mpq_clear(&rx->value);
    mpq_clear(&rr->value);
    mpq_clear(quotient);
    mpz_clear(rounded);
    mpz_clear(remainder);
    mpz_clear(doubled_num);

    return box_ptr(result, REAL_TAG_RATIONAL);
}

// Trigonometric functions - return symbolic expressions
public
Real_t Real$sin(Real_t x) {
    if (!is_boxed(x)) {
        double result = sin(x.d);
        // Check if result is exact (e.g., sin(0) = 0)
        if (result == 0.0 || result == 1.0 || result == -1.0) {
            return make_double(result);
        }
    }

    // Create symbolic expression
    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_SIN;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$cos(Real_t x) {
    if (!is_boxed(x)) {
        double result = cos(x.d);
        if (result == 0.0 || result == 1.0 || result == -1.0) {
            return make_double(result);
        }
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_COS;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$tan(Real_t x) {
    if (!is_boxed(x)) {
        double result = tan(x.d);
        if (result == 0.0) return make_double(0.0);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_TAN;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$asin(Real_t x) {
    if (!is_boxed(x)) {
        double result = asin(x.d);
        if (result == 0.0) return make_double(0.0);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_ASIN;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$acos(Real_t x) {
    if (!is_boxed(x)) {
        double result = acos(x.d);
        if (result == 0.0) return make_double(0.0);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_ACOS;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$atan(Real_t x) {
    if (!is_boxed(x)) {
        double result = atan(x.d);
        if (result == 0.0) return make_double(0.0);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_ATAN;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$atan2(Real_t y, Real_t x) {
    if (!is_boxed(y) && !is_boxed(x)) {
        double result = atan2(y.d, x.d);
        if (result == 0.0) return make_double(0.0);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_ATAN2;
    sym->left = y;
    sym->right = x;
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$exp(Real_t x) {
    if (!is_boxed(x)) {
        feclearexcept(FE_INEXACT);
        double result = exp(x.d);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_EXP;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$log(Real_t x) {
    if (!is_boxed(x)) {
        feclearexcept(FE_INEXACT);
        double result = log(x.d);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_LOG;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$log10(Real_t x) {
    if (!is_boxed(x)) {
        feclearexcept(FE_INEXACT);
        double result = log10(x.d);
        if (!fetestexcept(FE_INEXACT) && isfinite(result)) {
            return make_double(result);
        }
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_LOG10;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$abs(Real_t x) {
    if (!is_boxed(x)) {
        return make_double(fabs(x.d));
    }

    if (get_tag(x) == REAL_TAG_RATIONAL) {
        rational_t *r = get_ptr(x);
        rational_t *result = GC_MALLOC(sizeof(rational_t));
        mpq_init(&result->value);
        mpq_abs(&result->value, &r->value);
        return box_ptr(result, REAL_TAG_RATIONAL);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_ABS;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$floor(Real_t x) {
    if (!is_boxed(x)) {
        return make_double(floor(x.d));
    }

    if (get_tag(x) == REAL_TAG_RATIONAL) {
        rational_t *r = get_ptr(x);
        mpz_t result;
        mpz_init(result);
        mpz_fdiv_q(result, mpq_numref(&r->value), mpq_denref(&r->value));

        rational_t *rat = GC_MALLOC(sizeof(rational_t));
        mpq_init(&rat->value);
        mpq_set_z(&rat->value, result);
        mpz_clear(result);
        return box_ptr(rat, REAL_TAG_RATIONAL);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_FLOOR;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
Real_t Real$ceil(Real_t x) {
    if (!is_boxed(x)) {
        return make_double(ceil(x.d));
    }

    if (get_tag(x) == REAL_TAG_RATIONAL) {
        rational_t *r = get_ptr(x);
        mpz_t result;
        mpz_init(result);
        mpz_cdiv_q(result, mpq_numref(&r->value), mpq_denref(&r->value));

        rational_t *rat = GC_MALLOC(sizeof(rational_t));
        mpq_init(&rat->value);
        mpq_set_z(&rat->value, result);
        mpz_clear(result);
        return box_ptr(rat, REAL_TAG_RATIONAL);
    }

    symbolic_t *sym = GC_MALLOC(sizeof(symbolic_t));
    sym->op = SYM_CEIL;
    sym->left = x;
    sym->right = make_double(0);
    return box_ptr(sym, REAL_TAG_SYMBOLIC);
}

public
int Real$test() {
    GC_INIT();

    printf("=== Exact Number System Tests ===\n\n");

    // Test 1: Simple double arithmetic (fast path)
    printf("Test 1: 2.0 + 3.0\n");
    Real_t a = make_double(2.0);
    Real_t b = make_double(3.0);
    Real_t result = Real$plus(a, b);
    Text_t s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 2: Exact rational arithmetic
    printf("Test 2: 1/3 + 1/6\n");
    Real_t third = Real$from_rational(1, 3);
    Real_t sixth = Real$from_rational(1, 6);
    result = Real$plus(third, sixth);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 1/2\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 3: (1/3) * 3 should give exactly 1
    printf("Test 3: (1/3) * 3\n");
    Real_t three = Real$from_rational(3, 1);
    result = Real$times(third, three);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 1\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 4: sqrt(4) is exact
    printf("Test 4: sqrt(4)\n");
    Real_t four = Real$from_rational(4, 1);
    result = Real$sqrt(four);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 2\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 5: sqrt(2) * sqrt(2) stays symbolic
    printf("Test 5: sqrt(2) * sqrt(2)\n");
    Real_t two = Real$from_rational(2, 1);
    Real_t sqrt2 = Real$sqrt(two);
    result = Real$times(sqrt2, sqrt2);
    s = Real$value_as_text(result);
    printf("Result (symbolic): %s\n", Text$as_c_string(s));
    printf("Approximate value: %.17g\n", Real$as_float64(result, true));
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 6: Complex symbolic expression
    printf("Test 6: (sqrt(2) + 1) * (sqrt(2) - 1)\n");
    Real_t one = make_double(1.0);
    Real_t sqrt2_plus_1 = Real$plus(sqrt2, one);
    Real_t sqrt2_minus_1 = Real$minus(sqrt2, one);
    result = Real$times(sqrt2_plus_1, sqrt2_minus_1);
    s = Real$value_as_text(result);
    printf("Result (symbolic): %s\n", Text$as_c_string(s));
    printf("Approximate value: %.17g\n", Real$as_float64(result, true));
    printf("Expected: 1 (but stays symbolic)\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 7: Division creating rational
    printf("Test 7: 5 / 7\n");
    Real_t five = Real$from_int64(5);
    Real_t seven = Real$from_int64(7);
    result = Real$divided_by(five, seven);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 5/7\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 8: Power that stays exact
    printf("Test 8: 2^3\n");
    result = Real$power(make_double(2.0), make_double(3.0));
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 9: Decimal arithmetic
    printf("Test 8: 2^3\n");
    result = Real$power(make_double(2.0), make_double(3.0));
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 9: Currency calculation that fails with doubles
    printf("Test 9: 0.1 + 0.2 (classic floating point error)\n");
    Real_t dime = Real$from_rational(1, 10);
    Real_t two_dime = Real$from_rational(2, 10);
    result = Real$plus(dime, two_dime);
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Double arithmetic: %.17g\n", 0.1 + 0.2);
    printf("Expected: 0.3\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

    // Test 10: Rounding
    printf("Test 10: round(sqrt(2), 0.00001)\n");
    result = Real$rounded_to(sqrt2, Real$from_rational(1, 100000));
    s = Real$value_as_text(result);
    printf("Result: %s\n", Text$as_c_string(s));
    printf("Expected: 1.41421\n");
    printf("Is boxed: %s\n\n", is_boxed(result) ? "yes" : "no");

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
