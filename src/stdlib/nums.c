// Type info and methods for Num, Tomo's default numeric type: an exact
// computable real. See number.h/number-design.md for the representation.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fail.h"
#include "integers.h"
#include "nums.h"
#include "optionals.h"
#include "siphash.h"
#include "text.h"
#include "types.h"

// The `number` library reports domain errors (1/0, sqrt(-1), ln(0), ...) with
// a distinguished error value that propagates through arithmetic the way NaN
// does. Tomo's convention for arithmetic is to fail loudly at the point of the
// mistake instead, so operators check here and abort with the library's own
// explanation of what went wrong. Methods take the other route and return
// `none` -- see the `Num$opt_*` wrappers below.
static Num_t check(Num_t n) {
    if (unlikely(number_is_error(n))) fail("Arithmetic error: ", number_error_message(n));
    return n;
}

// The other route: a method whose argument can legitimately be out of domain
// (`(-1).sqrt()`, `(0).log()`) answers `none` rather than aborting, so callers
// can handle it with `or`/`!`. Same underlying error value, different policy.
static OptionalNum_t opt(Num_t n) {
    return number_is_error(n) ? NONE_NUM : n;
}

// The wrappers below are all the same two shapes, so they're generated:
// UNARY/BINARY abort on a domain error (operators, and methods that are total
// over the reals), UNARY_OPT/BINARY_OPT answer `none` instead (methods with a
// restricted domain).
#define UNARY(name, fn)                                                                                                \
    public Num_t Num$##name(Num_t x) {                                                                                 \
        return check(fn(x));                                                                                           \
    }
#define UNARY_OPT(name, fn)                                                                                            \
    public OptionalNum_t Num$##name(Num_t x) {                                                                         \
        return opt(fn(x));                                                                                             \
    }
#define BINARY(name, fn)                                                                                               \
    public Num_t Num$##name(Num_t x, Num_t y) {                                                                        \
        return check(fn(x, y));                                                                                        \
    }
#define BINARY_OPT(name, fn)                                                                                           \
    public OptionalNum_t Num$##name(Num_t x, Num_t y) {                                                                \
        return opt(fn(x, y));                                                                                          \
    }

// --- Operators (a domain error aborts) ---

BINARY(plus, number_add)
BINARY(minus, number_sub)
BINARY(times, number_mul)
BINARY(divided_by, number_div)
BINARY(modulo, number_mod)
BINARY(power, number_pow)
UNARY(negative, number_neg)

// --- Methods that are total over the reals (a domain error aborts) ---

UNARY(abs, number_abs)
UNARY(floor, number_floor)
UNARY(ceil, number_ceil)
UNARY(round, number_round)
UNARY(trunc, number_trunc)
UNARY(sin, number_sin)
UNARY(cos, number_cos)
UNARY(atan, number_atan)
UNARY(sinh, number_sinh)
UNARY(cosh, number_cosh)
UNARY(tanh, number_tanh)
UNARY(exp, number_exp)
BINARY(gcd, number_gcd)
BINARY(lcm, number_lcm)

// --- Methods with a restricted domain (out of domain is `none`) ---

UNARY_OPT(sqrt, number_sqrt)    // x < 0
UNARY_OPT(log, number_ln)       // x <= 0
UNARY_OPT(log10, number_log10)  // x <= 0
UNARY_OPT(log2, number_log2)    // x <= 0
UNARY_OPT(tan, number_tan)      // poles at pi/2 + k*pi
UNARY_OPT(asin, number_asin)    // |x| > 1
UNARY_OPT(acos, number_acos)    // |x| > 1
UNARY_OPT(inverse, number_inverse) // x == 0
BINARY_OPT(atan2, number_atan2) // (0, 0)

#undef UNARY
#undef UNARY_OPT
#undef BINARY
#undef BINARY_OPT

// --- Constants ---

public
Num_t Num$pi(void) {
    return number_pi();
}
public
Num_t Num$tau(void) {
    return number_tau();
}

// --- Approximation and conversion ---
//
// A Num is exact, so getting digits out of one means saying how many you
// want. These are the ways to ask.

// The decimal expansion to at most `digits` fractional places, correctly
// rounded (half-to-even). Exact values stop early rather than padding zeros:
// (1/4):digits(10) is "0.25", not "0.2500000000".
public
Text_t Num$digits(Num_t n, Int_t digits) {
    int64_t d = Int64$from_int(digits, false);
    if (unlikely(d < 0)) fail("Digit count can't be negative: ", digits);
    if (unlikely(d > UINT32_MAX)) fail("Digit count is too large: ", digits);
    return Text$from_str(number_to_string(n, (uint32_t)d, NULL));
}

// Whether `digits` fractional places capture the value exactly -- that is,
// whether `:digits(digits)` is the value itself rather than a rounding of it.
public
PUREFUNC bool Num$is_exact(Num_t n, Int_t digits) {
    int64_t d = Int64$from_int(digits, false);
    if (unlikely(d < 0)) fail("Digit count can't be negative: ", digits);
    if (unlikely(d > UINT32_MAX)) fail("Digit count is too large: ", digits);
    bool exact = false;
    (void)number_to_string(n, (uint32_t)d, &exact);
    return exact;
}

// The exact value written as a symbolic expression: "1/3", "sqrt(2)", "2*pi".
// This is also what `$n` interpolation and `say` use.
public
Text_t Num$symbolic(Num_t n) {
    return Text$from_str(number_to_symbolic(n));
}

// The exact value as TeX math-mode source: "\frac{1}{3}", "\sqrt{2}", "2\pi".
public
Text_t Num$tex(Num_t n) {
    return Text$from_str(number_to_tex(n));
}

// The nearest Float64, correctly rounded (half-to-even), with overflow going
// to infinity. Lossy by nature: this is the point where exactness is traded
// for hardware speed.
public
PUREFUNC Float64_t Num$to_float64(Num_t n) {
    return number_to_double(n);
}

// The value as an Int, or `none` if it isn't a whole number (or doesn't fit
// an Int64). Never rounds -- use :floor()/:ceil()/:round()/:trunc() first.
public
PUREFUNC OptionalInt_t Num$to_int(Num_t n) {
    bool ok = false;
    int64_t i = number_to_int64(n, &ok);
    return ok ? Int$from_int64(i) : NONE_INT;
}

// Whether the value is an exact rational (as opposed to an irrational like
// sqrt(2) or pi, which has no finite decimal or fractional form).
public
PUREFUNC bool Num$is_rational(Num_t n) {
    return number_is_rational(n);
}

// Whether the value is a whole number.
public
PUREFUNC bool Num$is_integer(Num_t n) {
    return number_is_integer(n);
}

public
Text_t Num$value_as_text(Num_t n) {
    // The exact form, not a decimal expansion: 1/3 prints as "1/3", sqrt(2) as
    // "sqrt(2)". A decimal would have to either terminate early (a lie about an
    // exact value) or run forever. `:as_text(digits)` gives the approximation.
    return Text$from_str(number_to_symbolic(n));
}

public
Text_t Num$as_text(const void *n, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!n) return Text("Num");
    Text_t text = Num$value_as_text(*(Num_t *)n);
    if (colorize) text = Text$concat(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

public
PUREFUNC int32_t Num$compare_value(Num_t x, Num_t y) {
    int cmp = number_compare(x, y);
    // 2 means "unordered": an error operand, or two general irrationals whose
    // difference the library could not decide within its precision cap.
    return cmp == 2 ? 0 : (int32_t)cmp;
}

public
PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Num$compare_value(*(Num_t *)x, *(Num_t *)y);
}

public
PUREFUNC bool Num$equal_value(Num_t x, Num_t y) {
    return number_equal(x, y);
}

public
PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Num$equal_value(*(Num_t *)x, *(Num_t *)y);
}

public
PUREFUNC uint64_t Num$hash(const void *vx, const TypeInfo_t *info) {
    (void)info;
    Num_t x = *(Num_t *)vx;
    // Equal numbers must hash equally, and equality here is mathematical, not
    // bitwise: sqrt(8) and 2*sqrt(2) are the same value in different shapes,
    // and a rational can be an immediate or a heap bigrat. The exact symbolic
    // form is canonical across all of that, so hash it rather than the bits.
    char *canonical = number_to_symbolic(x);
    return siphash24((void *)canonical, strlen(canonical));
}

public
PUREFUNC bool Num$is_none(const void *n, const TypeInfo_t *info) {
    (void)info;
    return ((Num_t *)n)->bits == NONE_NUM.bits;
}

public
const TypeInfo_t Num$info = {
    .size = sizeof(Num_t),
    .align = __alignof__(Num_t),
    .metamethods =
        {
            .compare = Num$compare,
            .equal = Num$equal,
            .hash = Num$hash,
            .as_text = Num$as_text,
            .is_none = Num$is_none,
        },
};
