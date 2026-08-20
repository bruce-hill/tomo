// Type info and methods for Num, Tomo's default numeric type: an exact
// computable real. See number.h/number-design.md for the representation.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <gc.h>
#include <gmp.h>
#include <stdio.h>

#include "fail.h"
#include "integers.h"
#include "nums.h"
#include "optionals.h"
#include "siphash.h"
#include "text.h"
#include "types.h"

// A Num that a Tomo program can actually hold is never the `number` library's
// error value: an operator that would produce one fails immediately, and a
// method that would produce one answers `none` instead. So error
// *propagation* -- the reason the library has an error value at all -- can't
// arise here, and only operations that can error from valid operands need
// checking. The rest are wrapped with no test at all.

// Out-of-line and noreturn, so the operators in nums.h can carry the error
// check inline without dragging fail()'s machinery into every call site.
public
_Noreturn void Num$arithmetic_error(Num_t bad) {
    fail("Arithmetic error: ", number_error_message(bad));
}

// The EQUALITY_DIGITS approximation, defined further down with the
// comparison logic that names the constant.
static Num_t rounded_for_equality(Num_t n);

// A method whose argument can legitimately be out of domain (`(-1):sqrt()`,
// `(0):log()`) answers `none` rather than aborting, so callers can handle it
// with `or`/`!`. Same underlying error value, different policy.
static OptionalNum_t opt(Num_t n) {
    return NUMBER_IS_ERROR(n) ? NONE_NUM : n;
}

#define UNARY(name, fn)                                                                                                \
    public Num_t Num$##name(Num_t x) {                                                                                 \
        return fn(x);                                                                                                  \
    }
// Rounding an irrational means deciding which side of an integer it falls
// on, and one case is genuinely undecidable: a value sitting so close to the
// boundary that no identity the engine recognizes places it, which refines
// forever and gives up. Failing there would contradict how equality already
// behaves -- agreement to EQUALITY_DIGITS settles what refinement can't -- so
// decide it the same way, from that approximation, which is a rational and so
// always rounds. The guard after it is unreachable: it would take a value
// whose decimal expansion is itself undecidable at that precision.
#define UNARY_ROUNDING(name, fn)                                                                                       \
    public Num_t Num$##name(Num_t x) {                                                                                 \
        Num_t result = fn(x);                                                                                          \
        if (unlikely(NUMBER_IS_ERROR(result))) {                                                                       \
            result = fn(rounded_for_equality(x));                                                                      \
            if (unlikely(NUMBER_IS_ERROR(result))) Num$arithmetic_error(result);                                       \
        }                                                                                                              \
        return result;                                                                                                 \
    }
#define UNARY_OPT(name, fn)                                                                                            \
    public OptionalNum_t Num$##name(Num_t x) {                                                                         \
        return opt(fn(x));                                                                                             \
    }
#define BINARY_OPT(name, fn)                                                                                           \
    public OptionalNum_t Num$##name(Num_t x, Num_t y) {                                                                \
        return opt(fn(x, y));                                                                                          \
    }

// --- Methods that cannot fail ---

UNARY(abs, number_abs)
UNARY(sin, number_sin)
UNARY(cos, number_cos)
UNARY(atan, number_atan)
UNARY(sinh, number_sinh)
UNARY(cosh, number_cosh)
UNARY(tanh, number_tanh)
UNARY(exp, number_exp)

// --- Rounding (see UNARY_ROUNDING) ---

UNARY_ROUNDING(floor, number_floor)
UNARY_ROUNDING(ceil, number_ceil)
UNARY_ROUNDING(round, number_round)
UNARY_ROUNDING(trunc, number_trunc)

// --- Methods with a restricted domain (out of domain is `none`) ---

UNARY_OPT(sqrt, number_sqrt)       // x < 0
UNARY_OPT(log, number_ln)          // x <= 0
UNARY_OPT(log10, number_log10)     // x <= 0
UNARY_OPT(log2, number_log2)       // x <= 0
UNARY_OPT(tan, number_tan)         // poles at pi/2 + k*pi
UNARY_OPT(asin, number_asin)       // |x| > 1
UNARY_OPT(acos, number_acos)       // |x| > 1
UNARY_OPT(inverse, number_inverse) // x == 0
BINARY_OPT(atan2, number_atan2)    // (0, 0)
BINARY_OPT(gcd, number_gcd)        // an irrational operand
BINARY_OPT(lcm, number_lcm)        // an irrational operand

#undef UNARY
#undef UNARY_ROUNDING
#undef UNARY_OPT
#undef BINARY_OPT

// --- Constructors ---
//
// Every one of these is exact. Converting *from* a Float64 is exact too --
// every finite double is a rational -- but it converts the double's actual
// value, so Float64(0.1) becomes 3602879701896397/36028797018963968 rather
// than 1/10. Writing `0.1` directly gives the tenth.

public
PUREFUNC Num_t Num$from_bool(Bool_t b) {
    return b ? NUMBER_ONE : NUMBER_ZERO;
}
public
PUREFUNC Num_t Num$from_byte(Byte_t b) {
    return number_from_int((int64_t)b);
}
public
PUREFUNC Num_t Num$from_int8(Int8_t i) {
    return number_from_int((int64_t)i);
}
public
PUREFUNC Num_t Num$from_int16(Int16_t i) {
    return number_from_int((int64_t)i);
}
public
PUREFUNC Num_t Num$from_int32(Int32_t i) {
    return number_from_int((int64_t)i);
}
public
PUREFUNC Num_t Num$from_int64(Int64_t i) {
    return number_from_int(i);
}
public
Num_t Num$from_int(Int_t i) {
    // An Int is unbounded, so route through its decimal form rather than
    // int64: `Int(10)**30` has no int64 to go through.
    if (likely(i.small & 1L)) return number_from_int(i.small >> 2L);
    return number_from_string(mpz_get_str(NULL, 10, *(mpz_t *)i.big));
}
public
PUREFUNC Num_t Num$from_float64(Float64_t n) {
    return number_from_double(n);
}
public
PUREFUNC Num_t Num$from_float32(Float32_t n) {
    return number_from_double((double)n);
}

// Parses the exact value the text denotes: "0.1" is a tenth, and "22/7" is
// that fraction, not either one's nearest double. `none` if it isn't a number.
public
OptionalNum_t Num$parse(Text_t text) {
    return opt(number_from_string(Text$as_c_string(text)));
}

// Euclidean division, matching the integer types: the unique integer q with
// x == y*q + r and 0 <= r < |y|. For y > 0 that's floor(x/y); for y < 0 it's
// ceil(x/y) (rounding down would leave a negative remainder).
public
Num_t Num$floor_divided_by(Num_t x, Num_t y) {
    Num_t q = number_div(x, y);
    if (unlikely(NUMBER_IS_ERROR(q))) Num$arithmetic_error(q);
    return number_is_negative(y) ? Num$ceil(q) : Num$floor(q);
}

// Euclidean modulus, matching the integer types: always non-negative,
// upholding x == y*(x//y) + (x mod y) exactly. (number_mod is *floored* --
// its result takes the divisor's sign -- so it can't be used directly.)
public
Num_t Num$modulo(Num_t x, Num_t y) {
    return Num$minus(x, Num$times(y, Num$floor_divided_by(x, y)));
}

// --- Comparison helpers ---

public
PUREFUNC Num_t Num$clamped(Num_t x, Num_t low, Num_t high) {
    return Num$compare_value(x, low) < 0 ? low : (Num$compare_value(x, high) > 0 ? high : x);
}
public
PUREFUNC bool Num$is_between(Num_t x, Num_t low, Num_t high) {
    // The bounds may be given in either order, matching Int/Float/Byte's
    // is_between (`(5):is_between(10, 1)` is yes for every other numeric type).
    return (Num$compare_value(low, x) <= 0 && Num$compare_value(x, high) <= 0)
           || (Num$compare_value(high, x) <= 0 && Num$compare_value(x, low) <= 0);
}
public
PUREFUNC Num_t Num$min(Num_t x, Num_t y) {
    return Num$compare_value(y, x) < 0 ? y : x;
}
public
PUREFUNC Num_t Num$max(Num_t x, Num_t y) {
    return Num$compare_value(y, x) > 0 ? y : x;
}

// Linear interpolation, exact: mix(0.5, x, y) is the true midpoint, and
// mix(1/3, 0, 1) is exactly a third rather than 0.3333333333333333.
public
Num_t Num$mix(Num_t amount, Num_t x, Num_t y) {
    return number_add(x, number_mul(amount, number_sub(y, x)));
}

// The value as a percentage, rounded to `precision` (a fraction, so 1% means
// whole percents and 0.01% means two decimal places).
public
Text_t Num$percent(Num_t n, Num_t precision) {
    if (unlikely(number_is_zero(precision))) fail("Percentage precision can't be zero");
    // Round to the nearest multiple of `precision`, then render the result as
    // a percentage: exact input in, exact percentage out.
    Num_t scaled = number_div(n, precision);
    Num_t rounded = number_mul(Num$round(scaled), precision);
    Num_t percent = number_mul(rounded, number_from_int(100));
    return Texts(Num$value_as_text(percent), Text("%"));
}

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

// How far to look for a terminating decimal before giving up and printing the
// symbolic form. Generous enough to cover any decimal a person is likely to
// have written, small enough that a value like 1/2^200 doesn't dump 200
// digits when "1/1606938...9376" says the same thing more clearly.
#define DISPLAY_DIGITS 30

public
Text_t Num$value_as_text(Num_t n) {
    // Whatever is printed here is exact -- never a rounded decimal, which
    // would quietly misreport the value. A terminating decimal is the
    // friendliest exact form when one exists (0.1 + 0.2 prints as "0.3"), so
    // try that first; otherwise fall back to the symbolic form, which is exact
    // for values no decimal can express: "1/3", "sqrt(2)", "2*pi".
    bool exact = false;
    char *decimal = number_to_string(n, DISPLAY_DIGITS, &exact);
    return Text$from_str(exact ? decimal : number_to_symbolic(n));
}

public
Text_t Num$as_text(const void *n, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!n) return Text("Num");
    Text_t text = Num$value_as_text(*(Num_t *)n);
    if (colorize) text = Text$concat(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

// Where exactness runs out. Comparing two irrationals is undecidable in
// general -- proving sqrt(3 + 2*sqrt(2)) == 1 + sqrt(2) takes symbolic
// reasoning no engine does in full -- so past this many digits of agreement,
// two values are treated as the same. Deep enough that nothing a program
// actually computes lands inside it by accident.
#define EQUALITY_DIGITS 40

// The value rounded to EQUALITY_DIGITS, as an exact rational. Comparing two
// of these always decides, where comparing the originals may not.
static Num_t rounded_for_equality(Num_t n) {
    bool exact = false;
    char *decimal = number_to_string(n, EQUALITY_DIGITS, &exact);
    Num_t rounded = number_from_decimal(decimal);
    return number_is_error(rounded) ? n : rounded;
}

// Reached only when number_compare answers 2 -- "couldn't decide" -- which
// means two general irrationals whose difference it can neither prove zero
// nor prove nonzero. Two answers are still available, in order of cost.
public
PUREFUNC int32_t Num$undecided_compare(Num_t x, Num_t y) {
    // Identical expressions are the same value, whatever that value is --
    // sin(2) and sin(2) need no refinement to be equal. The symbolic form is
    // canonical for structure, and is linear even for a heavily shared
    // expression (see number_to_symbolic's shared form).
    if (streq(number_to_symbolic(x), number_to_symbolic(y))) return 0;

    // Otherwise decide it numerically: agreement to EQUALITY_DIGITS counts as
    // equality. Rounding both to rationals first makes the comparison total.
    int cmp = number_compare(rounded_for_equality(x), rounded_for_equality(y));
    return cmp == 2 ? 0 : (int32_t)cmp;
}

public
PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Num$compare_value(*(Num_t *)x, *(Num_t *)y);
}

public
PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Num$equal_value(*(Num_t *)x, *(Num_t *)y);
}

public
PUREFUNC uint64_t Num$hash(const void *vx, const TypeInfo_t *info) {
    (void)info;
    // Equal values must hash equally, and equality here is mathematical, not
    // bitwise: sqrt(8) and 2*sqrt(2) are the same number wearing different
    // shapes, a rational can be an immediate or a heap bigrat, and two
    // irrationals count as equal once they agree to EQUALITY_DIGITS. The
    // decimal expansion to that many digits is the one form all three of
    // those collapse to, so hash that.
    char *digits = number_to_string(*(Num_t *)vx, EQUALITY_DIGITS, NULL);
    return siphash24((void *)digits, strlen(digits));
}

public
PUREFUNC bool Num$is_none(const void *n, const TypeInfo_t *info) {
    (void)info;
    return ((Num_t *)n)->bits == NONE_NUM.bits;
}

// Serialized as the exact symbolic form -- "1/3", "sqrt(2)", "1 + pi" -- and
// read back by parsing it. That form is the only representation that covers
// every tier: a binary encoding of the tagged word would be writing out
// pointers, and a decimal expansion doesn't terminate for most fractions and
// doesn't exist at all for an irrational.
//
// It also keeps the format readable and independent of the engine's
// internals, so stored data survives changes to how a value is represented.
static void Num$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info) {
    (void)pointers, (void)info;
    const char *symbolic = number_to_symbolic(*(Num_t *)obj);
    size_t len = strlen(symbolic);
    Int64$serialize(&(int64_t){(int64_t)len}, out, pointers, &Int64$info);
    fwrite(symbolic, 1, len, out);
}

static void Num$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *info) {
    (void)pointers, (void)info;
    int64_t len = 0;
    Int64$deserialize(in, &len, pointers, &Int64$info);
    if (unlikely(len < 0)) fail("Corrupted Num in serialized data");
    char *buf = GC_MALLOC_ATOMIC((size_t)len + 1);
    // A corrupt length prefix can ask for more than the collector can give;
    // GC_MALLOC_ATOMIC answers NULL rather than aborting, and fread must not be
    // handed that.
    if (unlikely(buf == NULL)) fail("Corrupted Num in serialized data");
    if (fread(buf, 1, (size_t)len, in) != (size_t)len) fail("Corrupted Num in serialized data");
    buf[len] = '\0';
    Num_t n = number_from_symbolic(buf);
    if (unlikely(number_is_error(n))) fail("Couldn't deserialize this Num: ", buf);
    *(Num_t *)obj = n;
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
            .serialize = Num$serialize,
            .deserialize = Num$deserialize,
        },
};
