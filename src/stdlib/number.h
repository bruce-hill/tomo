// number.h declares a general-purpose exact numeric datatype.
//
// See number-design.md for the full design. Three tiers: small rationals packed
// into an immediate 64-bit value (no heap); heap-allocated big rationals
// backed by a GMP-based bigint; and irrational reals, themselves in two forms.
// The first is a closed symbolic form a + b*pi or a + b*sqrt(n) (a, b
// rational), where identities like sqrt(2)*sqrt(2) == 2 and pi - pi == 0 hold
// exactly, and this form is preferred whenever a result fits it. The second
// is a general constructive-real DAG (pi + sqrt(2), sin(2), exp(pi), ...)
// used whenever a result doesn't unify into the first form: still correct
// to any requested precision, but not a closed symbolic form (identities
// like sin(x)^2+cos(x)^2 == 1 aren't recognized, and a comparison or
// division that would need to decide such an identity gives up rather than
// loop forever; see number_compare). Genuine domain errors (1/0, ln of a
// non-positive number, asin/acos outside [-1,1], a pole like tan(pi/2), ...)
// return the distinguished error value.

#ifndef NUMBER_H
#define NUMBER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "layout/num.h" // IWYU pragma: export

// A Num is a 64-bit tagged value (see number-design.md "Tagging"):
//   low bits 01: small rational immediate
//   low bits 00: pointer to a GC-allocated heap object
//   low bits 11: the error value
// Heap tiers are GC-allocated, so copying a Num is always safe and always
// cheap: there is no ownership to transfer and nothing to release. The same
// goes for the strings number_to_string/number_to_symbolic/number_to_tex
// return.

// The distinguished error value: the result of undefined operations (1/0,
// parse failure, NaN conversion, ...). Errors propagate: any arithmetic
// involving an error yields an error.
#define NUMBER_ERROR ((Num_t){.bits = 0x3})

// --- Constructors ---

Num_t number_from_int(int64_t value);
// Any int64/int64 ratio; denominator 0 yields the error value.
Num_t number_from_ratio(int64_t numerator, int64_t denominator);
// Exact conversion, since every finite double is a rational. NaN/Inf -> error.
// NOTE: for a SOURCE LITERAL, this is a trap: number_from_double(3.15)
// converts 3.15's nearest double (3.14999999999999991...), not 63/20.
// Reach for number_from_decimal("3.15") instead, which parses the written
// decimal exactly. number_from_double is for a double you already hold at
// runtime, not for text you could have written out.
Num_t number_from_double(double value);
// Accepts "123", "-4.56", "6.02e23", and "22/7" (denominator unsigned),
// with optional surrounding whitespace. Anything else -> error.
Num_t number_from_string(const char *str);
// The exact decimal-literal constructor: the obvious, named way to turn a
// written decimal ("3.15", "-4.56", "6.02e23", "100") into the exact
// rational it denotes, with no double round-trip (63/20 for "3.15", never a
// nearby double). This is what to emit/write for a source literal instead of
// number_from_double. Same integer/fixed-point/scientific grammar and
// leading-whitespace/sign handling as number_from_string, but the "22/7"
// ratio form is rejected (it is not decimal notation, so use
// number_from_ratio or number_from_string for a ratio). Anything else ->
// error.
Num_t number_from_decimal(const char *str);

// --- Compile-time literal construction ---
//
// For a code generator (e.g. a language that compiles to C) that wants to
// turn a source literal into a `Num_t` with zero runtime cost, with no
// number_from_string call, no heap allocation, nothing but a 64-bit immediate
// load. NUMBER_SMALL(num, den) is a genuine C constant expression, usable
// directly in a `static const number x = NUMBER_SMALL(...);` file-scope
// initializer and not just something an optimizer happens to fold at -O1+,
// that packs an ALREADY REDUCED rational straight into the immediate tier,
// the same representation small_make (number.c) builds at runtime, bypassing
// every runtime code path entirely.
//
// IMPORTANT for decimal literals ("3.15", ...): reduce the literal to an
// exact rational yourself (e.g. "3.15" -> 315/100 -> gcd-reduce -> 63/20)
// and pass that pair here. Do NOT round-trip through a C `double` (i.e.
// don't emit `number_from_double(3.15)`), since 3.15 is not exactly
// representable as a double, so that would silently give this library's
// callers a different value than the source literal wrote, defeating the
// "correct by default" purpose the double fallback exists to be an
// alternative to.
//
// The caller (not this macro) is responsible for satisfying every one of
// these before use. Getting any of them wrong produces a `Num_t` that
// looks well-formed but silently violates the small-rational canonical-form
// invariant every other part of this library relies on (in particular,
// number_equal's bit-equality fast path for two small rationals):
//   - num and den must already be in lowest terms: gcd(|num|, den) == 1
//   - den must be > 0 (the sign, if any, lives in num, never in den)
//   - NUMBER_SMALL_NUM_MIN <= num <= NUMBER_SMALL_NUM_MAX
//   - 1 <= den <= NUMBER_SMALL_DEN_MAX
// A value outside these bounds (or not yet reduced) needs a real bigint and
// so cannot be a NUMBER_SMALL literal: fall back to one number_from_decimal
// (or number_from_string for a ratio) call for it instead. Both are exact,
// with no double round-trip, and the result can be cached (e.g. in a lazily
// initialized static) so a "very big or precise" literal still only pays
// that cost once, not on every use.
//
// This is a small, deliberately exposed slice of the internal packing
// scheme described in number-design.md ("Tagging"). The number test suite
// cross-checks it against number_from_ratio's own runtime packing, so a
// future change to the internal representation can't silently break it
// without a test failing first.
#define NUMBER_SMALL_NUM_MIN (-2147483647LL) // -(2^31 - 1); zero can't be negative, so -2^31 itself doesn't arise
#define NUMBER_SMALL_NUM_MAX 2147483647LL // 2^31 - 1
#define NUMBER_SMALL_DEN_MAX 1073741823u // 2^30 - 1
#define NUMBER_SMALL(num, den)                                                                                         \
    ((Num_t){((uint64_t)(uint32_t)(int32_t)(num) << 32) | ((uint64_t)(uint32_t)(den) << 2) | 0x1u})

// The three most common numeric literals, #defined for a code generator
// (or anyone else) that would otherwise write NUMBER_SMALL(0, 1) etc. --
// same zero-cost compile-time constant, just named. Compare NUMBER_ERROR
// above, which does the same for the (unrelated) error immediate.
#define NUMBER_ZERO NUMBER_SMALL(0, 1)
#define NUMBER_ONE NUMBER_SMALL(1, 1)
#define NUMBER_NEG_ONE NUMBER_SMALL(-1, 1)

// --- Predicates ---

// Whether x is the error value: the same test number_is_error does, written
// as a macro for callers that can't afford a call. The low two bits are the
// tag and 11 is the error tag (see "Tagging" above and number-design.md), so
// this is one mask-and-compare on a value already in a register.
#define NUMBER_IS_ERROR(x) (((x).bits & 0x3) == 0x3)

bool number_is_error(Num_t x);
// A static, human-readable description of why x is the error value, e.g.
// "division by zero" or "square root of a negative number", or NULL if x is
// not the error value. Never free the returned string. When an error
// propagates from a combination of two already-erroneous values (e.g.
// 2/0 + sqrt(-1)), the message is whichever operand's reason the
// implementation happened to check first; both are equally "the" answer.
const char *number_error_message(Num_t x);
bool number_is_zero(Num_t x);
bool number_is_negative(Num_t x);
// True for tiers 1 and 2 (exact rationals); false for irrational reals.
bool number_is_rational(Num_t x);
// True iff x is provably an integer (a rational with denominator 1). Like
// number_is_rational, a general constructive-real value that happens to be
// mathematically an integer without this library recognizing it reports
// false, which means "not provably an integer", not "provably not".
bool number_is_integer(Num_t x);
// -1, 0, or +1. The sign of the error value is reported as 0.
int number_sign(Num_t x);

// --- Arithmetic ---
//
// number_add/sub/mul/div/neg are ordinary declarations here, but, unless
// NUMBER_STATS is defined, also get a *definition* just below: a forced-
// inline fast path (gnu_inline + always_inline) for the common case (both
// operands already the small-immediate tier; for add/sub/mul, also no
// overflow out of it), falling straight through to number_*_general (the
// unchanged, full implementation, in number.c) for everything else:
// fractions, bigrat, irrational, error, overflow.
//
// This exists because the fast path alone can't get inlined the ordinary
// way: number_add et al.'s *full* bodies (error checks, irrational-kind
// checks, the general/bigrat fallback) are too large for a compiler's own
// inlining heuristic to accept, even across translation units under LTO,
// which was checked directly (disassembly of an LTO build still showed a real
// `call` for the full function). gnu_inline + always_inline is what lets a
// *narrow slice* of that logic, just the part worth duplicating at every call
// site, get inlined unconditionally, while the rest stays a single,
// un-duplicated implementation reached by a real call.
//
// A caller never needs to know any of this: number_add(a, b) is still the
// name to call. The fast path is intentionally narrow and simple enough to
// be obviously correct by inspection (see the definitions below): it
// mirrors number_addsub/number_mul/number_div/number_neg's own tier-1
// integer sub-path exactly, using only the bit layout NUMBER_SMALL already
// documents as public, with __builtin_*_overflow guarding every case that
// could leave the immediate tier's bounds. The number test suite cross-checks
// every fast-path outcome against number_*_general bit-for-bit, so a
// future change to the internal representation can't silently desync them.
//
// NUMBER_STATS is the one case that deliberately opts out: its whole
// purpose is accurate tier-dispatch counts (see number_stats_dump below),
// and a call that resolves inline in the caller's own translation unit
// never reaches number.c's counters. An instrumented build accepts losing
// this optimization in exchange for those counts actually being trustworthy.

// The full, general implementation, always available under this name
// regardless of NUMBER_STATS, and what number_add et al.'s fast path (when
// present) falls through to for anything outside the small-immediate tier.
Num_t number_add_general(Num_t a, Num_t b);
Num_t number_sub_general(Num_t a, Num_t b);
Num_t number_mul_general(Num_t a, Num_t b);
Num_t number_div_general(Num_t a, Num_t b); // b == 0 -> error
Num_t number_neg_general(Num_t x);

#ifdef NUMBER_STATS
Num_t number_add(Num_t a, Num_t b);
Num_t number_sub(Num_t a, Num_t b);
Num_t number_mul(Num_t a, Num_t b);
Num_t number_div(Num_t a, Num_t b); // b == 0 -> error
Num_t number_neg(Num_t x);
#else
// number_add/number_sub's integer fast paths do whole-word tagged
// arithmetic, with no field extraction and no repack, following Daan Leijen,
// "What About the Integer Numbers? Fast Arithmetic with Tagged Integers"
// (MSR-TR-2022-17), adapted from its litbit/sofa techniques to this
// layout. The pieces that make it sound here:
//   - An integer immediate's low 32 bits are exactly 0x5 (denominator 1
//     in bits 2-31, tag 01 in bits 0-1), so "is an integer immediate" is
//     one mask-and-compare per operand, with no separate tag and denominator
//     checks. No other word matches: tag 01 is only ever a small rational.
//   - Adding two such words adds the numerator fields as an *independent*
//     32-bit addition: the low halves contribute 5 + 5 = 10, which cannot
//     carry into bit 32. (Subtraction: 5 - 5 = 0, no borrow.)
//   - Bit 63 of each word IS the numerator field's sign bit, so the classic
//     signed-overflow tests, ((a^w) & (b^w)) < 0 for addition and
//     ((a^b) & (a^w)) < 0 for subtraction, work directly on the tagged
//     words, no sign-extension needed.
//   - A no-overflow sum can still be INT32_MIN, one past
//     NUMBER_SMALL_NUM_MIN (which is -(2^31 - 1)); the explicit high-field
//     compare excludes it.
//   - Retagging is a single +/-0x5 on the whole word (low half is 0xA
//     after an add, 0x0 after a subtract; either way the numerator field
//     is untouched since no carry/borrow crosses bit 32).
extern inline __attribute__((gnu_inline, always_inline)) Num_t number_add(Num_t a, Num_t b) {
    if (__builtin_expect((a.bits & 0xFFFFFFFFu) == 0x5u && (b.bits & 0xFFFFFFFFu) == 0x5u, 1)) {
        uint64_t w = a.bits + b.bits;
        if (__builtin_expect((int64_t)((a.bits ^ w) & (b.bits ^ w)) >= 0 && (w >> 32) != 0x80000000u, 1))
            return (Num_t){w - 0x5u};
    }
    return number_add_general(a, b);
}

extern inline __attribute__((gnu_inline, always_inline)) Num_t number_sub(Num_t a, Num_t b) {
    if (__builtin_expect((a.bits & 0xFFFFFFFFu) == 0x5u && (b.bits & 0xFFFFFFFFu) == 0x5u, 1)) {
        uint64_t w = a.bits - b.bits; // low halves cancel exactly: low32(w) == 0
        if (__builtin_expect((int64_t)((a.bits ^ b.bits) & (a.bits ^ w)) >= 0 && (w >> 32) != 0x80000000u, 1))
            return (Num_t){w + 0x5u};
    }
    return number_sub_general(a, b);
}

extern inline __attribute__((gnu_inline, always_inline)) Num_t number_mul(Num_t a, Num_t b) {
    if (__builtin_expect((a.bits & 3) == 1 && (b.bits & 3) == 1, 1)) {
        uint64_t da = (a.bits >> 2) & NUMBER_SMALL_DEN_MAX, db = (b.bits >> 2) & NUMBER_SMALL_DEN_MAX;
        if (__builtin_expect(da == 1 && db == 1, 1)) { // integer sub-path, matching number_mul
            int64_t na = (int32_t)(a.bits >> 32), nb = (int32_t)(b.bits >> 32), prod;
            if (!__builtin_mul_overflow(na, nb, &prod) && prod <= NUMBER_SMALL_NUM_MAX && prod >= NUMBER_SMALL_NUM_MIN)
                return NUMBER_SMALL(prod, 1);
        }
    }
    return number_mul_general(a, b);
}

extern inline __attribute__((gnu_inline, always_inline)) Num_t number_div(Num_t a, Num_t b) {
    if (__builtin_expect((a.bits & 3) == 1 && (b.bits & 3) == 1, 1)) {
        uint64_t da = (a.bits >> 2) & NUMBER_SMALL_DEN_MAX, db = (b.bits >> 2) & NUMBER_SMALL_DEN_MAX;
        if (__builtin_expect(da == 1 && db == 1, 1)) {
            int64_t na = (int32_t)(a.bits >> 32), nb = (int32_t)(b.bits >> 32);
            // Unlike add/sub/mul, integer division has no shortcut in
            // general, since 3/4 is a genuine fraction needing real gcd
            // reduction, which number_div_general (and number_from_ratio64
            // beneath it) already does correctly. This inlines only the
            // sub-case where that reduction is unnecessary: b evenly
            // divides a, so the quotient is already an integer, and
            // |a/b| <= |a| means it's automatically back in bounds, with
            // no separate overflow check needed, unlike the other three.
            if (nb != 0 && na % nb == 0) return NUMBER_SMALL(na / nb, 1);
        }
    }
    return number_div_general(a, b);
}

extern inline __attribute__((gnu_inline, always_inline)) Num_t number_neg(Num_t x) {
    if (__builtin_expect((x.bits & 3) == 1, 1)) {
        // Negating an already-in-bounds small rational is always back in
        // bounds (|-num| == |num|; NUMBER_SMALL_NUM_MIN is -(2^31-1), not
        // INT32_MIN, specifically so this never overflows; see its own
        // comment above), so no bounds check is needed, unlike add/sub/mul.
        int32_t n = (int32_t)(x.bits >> 32);
        return (Num_t){((uint64_t)(uint32_t)(-n) << 32) | (x.bits & 0xFFFFFFFFu)};
    }
    return number_neg_general(x);
}
#endif

Num_t number_abs(Num_t x);
Num_t number_inverse(Num_t x); // 1/x; x == 0 -> error

// Greatest common divisor and least common multiple, generalized to exact
// rationals: gcd is the largest g >= 0 with a/g and b/g both integers, lcm the
// smallest l >= 0 that is an integer multiple of both. For integers these are
// the ordinary gcd/lcm; for fractions gcd(1/2, 1/3) == 1/6 and
// lcm(1/2, 1/3) == 1. Both results are non-negative regardless of operand
// signs; gcd(0, x) == |x| (gcd(0, 0) == 0) and lcm(0, x) == 0. An irrational
// operand (sqrt(2), pi, ...) has no such divisor structure -> error.
Num_t number_gcd(Num_t a, Num_t b);
Num_t number_lcm(Num_t a, Num_t b);

// --- Rounding ---
//
// All four return an integer-valued number (still a full-precision exact
// value: floor of 2^100 + 1/2 is the exact bigint 2^100, not a truncation).
// Exact and cheap for the rational tiers. For an irrational value the
// integer part is decided by refining approximations, which almost always
// resolves immediately. The one genuinely undecidable case, a general
// (non-symbolic-form) irrational lying exactly on a rounding boundary
// without any identity this library recognizes proving it (e.g.
// floor(asin(sin(1))) when that value is suspiciously close to 1), gives
// up past an internal precision cap and returns the error value, the same
// escape hatch number_compare documents. Values in a closed symbolic form
// (pi, sqrt(n), and rational combinations thereof) are provably irrational
// and never hit the cap in practice.

Num_t number_floor(Num_t x); // largest integer <= x
Num_t number_ceil(Num_t x); // smallest integer >= x
Num_t number_trunc(Num_t x); // round toward zero: floor for x >= 0, ceil for x < 0
Num_t number_round(Num_t x); // nearest integer, ties to even (round-half-even,
                             // matching number_to_string's digit rounding)

// Floored modulus: a - b*floor(a/b). The result has b's sign (or is zero),
// lies in [0, b) for b > 0 (the right shape for periodic range reduction),
// and satisfies a == b*floor(a/b) + number_mod(a, b) exactly. b == 0 ->
// error; an undecidable floor (see above) propagates its error.
Num_t number_mod(Num_t a, Num_t b);

// --- Tier 3: irrational reals ---

// The constant pi. A cached singleton after the first call (see number.c):
// repeat calls hand back the same object rather than allocating, and share
// whatever precision a prior call already refined it to.
Num_t number_pi(void);
// 2*pi. Same caching as number_pi.
Num_t number_tau(void);
// Square root. Exact for a rational x: sqrt(4) == 2 and sqrt(2)*sqrt(2) == 2
// exactly. For an irrational x (already-irrational input, e.g. sqrt(pi) or
// a nested root like sqrt(sqrt(2))), evaluated by the general
// constructive-real engine instead: correct to any requested precision, but
// not a closed symbolic form (number_is_rational is false, and identities
// like sqrt(x)*sqrt(x) == x aren't recognized symbolically for such x).
// x < 0 -> error.
Num_t number_sqrt(Num_t x);
// sqrt(2). Same caching as number_pi; equivalent to number_sqrt(number_from_int(2)).
Num_t number_sqrt2(void);

// e^x. Exact for x == 0 (-> 1); otherwise evaluated by the general
// constructive-real engine (correct to any requested precision, but not a
// closed symbolic form: number_is_rational is false, and identities like
// ln(exp(x)) == x are not recognized symbolically).
Num_t number_exp(Num_t x);
// Natural log. x <= 0 -> error. Exact for x == 1 (-> 0); otherwise general.
Num_t number_ln(Num_t x);
// Base-10 log = ln(x)/ln(10). Same domain as number_ln.
Num_t number_log10(Num_t x);
// Base-2 log = ln(x)/ln(2). Same domain as number_ln. Exact for x == 2^n
// (any integer n): log2(8) == 3, log2(1/4) == -2.
Num_t number_log2(Num_t x);

// sin/cos, range-reduced modulo 2*pi internally so large arguments are
// still exact (to the requested output precision). Exact for x == 0
// (sin -> 0, cos -> 1); otherwise the general engine.
Num_t number_sin(Num_t x);
Num_t number_cos(Num_t x);
// tan(x) = sin(x)/cos(x); poles (x near pi/2 + k*pi) -> error.
Num_t number_tan(Num_t x);

// atan(x): defined for all x. Exact at x == 0.
Num_t number_atan(Num_t x);
// atan2(y, x): the angle in (-pi, pi] of the point (x, y), i.e. atan(y/x)
// placed in the correct quadrant by the signs of x and y. Exact on the axes
// and wherever number_atan is (atan2(1, 1) == pi/4, atan2(1, -1) == 3*pi/4).
// atan2(0, 0) is undefined -> error (unlike C's atan2, which returns 0).
Num_t number_atan2(Num_t y, Num_t x);
// asin/acos: domain |x| <= 1 -> error otherwise. Exact at x == 0, 1, -1.
// Accept an already-irrational x too (e.g. asin(sin(0.5))), same as
// number_sqrt, which they're built on: falls back to the general engine.
Num_t number_asin(Num_t x);
Num_t number_acos(Num_t x);

// Hyperbolic functions, via exp. Exact at x == 0 (sinh/tanh -> 0, cosh -> 1).
Num_t number_sinh(Num_t x);
Num_t number_cosh(Num_t x);
Num_t number_tanh(Num_t x);

// x^y. Exact for y == 0 (-> 1, including 0^0), y == 1, integer y (any
// sign), and y == 1/2 with x >= 0 (via number_sqrt). 0^negative -> error;
// negative x with non-integer y -> error (complex result); otherwise the
// general engine via exp(y*ln(x)), x > 0.
Num_t number_pow(Num_t x, Num_t y);

// --- Comparison ---
//
// Same forced-inline fast path treatment as the arithmetic operators above
// (see the comment there for the full rationale): number_compare and
// number_equal are ordinary declarations, plus, unless NUMBER_STATS is
// defined, a gnu_inline/always_inline definition covering the case both
// operands are already tier-1, falling through to number_*_general
// (number.c) for everything else.

// Returns -1, 0, or +1; returns 2 ("unordered") if either argument is the
// error value. Exact for every representable value: equal values are
// recognized symbolically (sqrt(8) == 2*sqrt(2)) when possible, and
// otherwise decided by refining approximations. Also returns 2 in the one
// case that's genuinely undecidable: two general (non-symbolic-form)
// irrational values whose difference is zero without any identity this
// library recognizes proving it (e.g. sin(x)^2+cos(x)^2 vs. 1) refine up to
// an internal precision cap and then give up, rather than loop forever.
int number_compare_general(Num_t a, Num_t b);

// number_compare, but giving up after max_prec bits of refinement instead of
// the internal cap. Same answers, except that a difference too small to be
// excluded from zero within max_prec bits is reported unordered (2) rather
// than resolved. For a caller that applies its own "close enough counts as
// equal" threshold to the undecided case, refining past that threshold is
// work whose result gets discarded: passing the matching precision here
// makes the two agree instead of leaving a gap between them (see
// Num$compare_value, whose cap is NUM_EQUALITY_PREC).
int number_compare_capped_general(Num_t a, Num_t b, uint32_t max_prec);

// Exact equality. The error value is not equal to anything, including
// itself. See number_compare for the one undecidable case (reported as
// not-equal here, since equality can't be confirmed).
bool number_equal_general(Num_t a, Num_t b);

#ifdef NUMBER_STATS
int number_compare(Num_t a, Num_t b);
int number_compare_capped(Num_t a, Num_t b, uint32_t max_prec);
bool number_equal(Num_t a, Num_t b);
#else
extern inline
    __attribute__((gnu_inline, always_inline)) int number_compare_capped(Num_t a, Num_t b, uint32_t max_prec) {
    // The tier-1 fast path is precision-independent, so it is exactly
    // number_compare's; only the refinement past it takes the cap.
    if (__builtin_expect((a.bits & 3) == 1 && (b.bits & 3) == 1, 1)) {
        if (a.bits == b.bits) return 0;
        int64_t l = (int32_t)(a.bits >> 32) * (int64_t)((b.bits >> 2) & NUMBER_SMALL_DEN_MAX);
        int64_t r = (int32_t)(b.bits >> 32) * (int64_t)((a.bits >> 2) & NUMBER_SMALL_DEN_MAX);
        return (l > r) - (l < r);
    }
    return number_compare_capped_general(a, b, max_prec);
}

extern inline __attribute__((gnu_inline, always_inline)) int number_compare(Num_t a, Num_t b) {
    if (__builtin_expect((a.bits & 3) == 1 && (b.bits & 3) == 1, 1)) {
        if (a.bits == b.bits) return 0; // same immediate
        // Cross-multiply, matching number_compare_general's own TAG_SMALL
        // branch exactly: |num| <= 2^31, den < 2^30, so both products fit
        // in int64.
        int64_t l = (int32_t)(a.bits >> 32) * (int64_t)((b.bits >> 2) & NUMBER_SMALL_DEN_MAX);
        int64_t r = (int32_t)(b.bits >> 32) * (int64_t)((a.bits >> 2) & NUMBER_SMALL_DEN_MAX);
        return (l > r) - (l < r);
    }
    return number_compare_general(a, b);
}

extern inline __attribute__((gnu_inline, always_inline)) bool number_equal(Num_t a, Num_t b) {
    // Single-operand tag test (Leijen, MSR-TR-2022-17, section 2.5): when
    // a is a small immediate, a.bits == b.bits fully decides equality
    // *regardless of what b is*, so only a's tag needs checking, and the
    // test disappears entirely when a is a NUMBER_SMALL constant. Sound
    // because:
    //   - small rationals are canonical (number-design.md): equal smalls are
    //     bit-identical;
    //   - no other word can bit-equal a tag-01 word (pointers are tag 00,
    //     errors tag 11);
    //   - a non-small value numerically equal to a small one cannot be
    //     *reported* equal by the general path either: big rationals are
    //     canonical (a value never exists in both tiers), reals are
    //     irrational by construction (a zero coefficient never builds a
    //     real node), and an IRRATIONAL node's comparison against a
    //     rational only ever proves nonzero or gives up, both of which are
    //     reported not-equal (see number_compare's contract above).
    // number_compare itself still needs both tags: ordering two *unequal*
    // values requires b's fields, not just a bit comparison.
    if (__builtin_expect((a.bits & 3) == 1, 1)) return a.bits == b.bits;
    return number_equal_general(a, b);
}
#endif

// The smaller (number_min) or larger (number_max) of a and b, returned as the
// exact original value, a fresh reference the caller owns. Exact comparison
// decides the common case (rationals and known symbolic forms always decide);
// only for two general irrationals whose order number_compare cannot resolve
// within its precision cap does the decision fall back to comparing them
// rounded to `digits` fractional digits, which guarantees a definite,
// terminating answer rather than propagating the "unordered" result. On a
// tie, whether an exact equality or indistinguishability at `digits`, the
// first operand (a) is returned. An error operand propagates. (`digits` is ignored
// whenever exact comparison succeeds, which is nearly always.)
Num_t number_min(Num_t a, Num_t b, uint32_t digits);
Num_t number_max(Num_t a, Num_t b, uint32_t digits);

// --- Conversion ---

// Correctly rounded (round-to-nearest-even), including subnormals and
// overflow to infinity. The error value converts to NaN.
double number_to_double(Num_t x);

// Checked exact conversion: the value of x if x is an integer (see
// number_is_integer) that fits in int64_t, else 0. If ok is non-NULL it is
// set to whether the conversion succeeded, so 0 with *ok true really is the
// value zero. Never rounds: pass x through number_trunc (or floor/ceil/
// round) first to convert a non-integer.
int64_t number_to_int64(Num_t x, bool *ok);

// Decimal representation with at most max_frac_digits fractional digits,
// correctly rounded (round-half-even) when the value needs more digits.
// Trailing zeros are not produced when the value is exactly representable;
// the fractional part (and the '.') is omitted for integers. If is_exact is
// non-NULL, it is set to whether the returned string is the exact value.
// Returns a GC-allocated string (do not free it); "(error)" for the error
// value.
char *number_to_string(Num_t x, uint32_t max_frac_digits, bool *is_exact);

// Exact symbolic form: "42", "-7/2", "pi", "2*pi", "sqrt(6)", "3 - sqrt(2)",
// "1/2 + sqrt(5)/2" for the closed forms; an expression-tree description
// like "sin(2)" or "pi + sqrt(2)" for general constructive-real values
// (see the file header), still an exact description of the value, just
// not reduced to a closed algebraic form. Repeated identical factors of a
// product collapse into powers, so pi*pi*pi is "pi^3", not "pi*pi*pi",
// where "identical" is decided syntactically (shared nodes, equal
// rationals, same-shaped subtrees), never by refining approximations.
// Parentheses appear only where an operand would misread without them
// ("(1 + pi)*sin(2)"), never around the whole expression.
// Always exact, unlike a decimal expansion, which may not terminate.
// Returns a GC-allocated string (do not free it).
char *number_to_symbolic(Num_t x);

// The inverse of number_to_symbolic: reads back the exact expression it
// prints ("1/3", "sqrt(2)", "1 + pi", "sin(2)*pi", "pi^3"), so an exact
// value can round-trip through text. Whitespace around operators is
// optional. Anything outside that grammar -> error.
Num_t number_from_symbolic(const char *str);

// The same exact forms as number_to_symbolic, rendered as TeX math-mode
// source: "42", "-\frac{7}{2}", "\pi", "2\pi", "\sqrt{6}", "3 - \sqrt{2}",
// "\frac{1}{2} + \frac{\sqrt{5}}{2}" for the closed forms; an
// expression-tree description like "\sin(2)" or "\pi + \sqrt{2}" for
// general constructive-real values (exp renders as "e^{...}", atan as
// "\arctan", division as "\frac{...}{...}", and repeated identical factors
// collapse into powers: "\pi^{3}"). Returns a GC-allocated string (do not
// free it).
char *number_to_tex(Num_t x);

// --- Instrumentation (opt-in via -DNUMBER_STATS) ---

#ifdef NUMBER_STATS
#include <stdio.h>
// Prints a summary of dispatch counts collected since startup: how many
// number_add/sub/mul/div/compare calls landed in each tier (immediate
// small-rational, heap bigrat, real/irrational), how many small-rational
// results promoted out of the immediate tier, bigint allocation count, and
// constructive-real DAG node memo-hit/recompute counts by operation. Meant
// to answer "where do real workloads actually spend their calls," not "how
// slow is this operation" (that's a profiler's job, not the library's).
// Safe to call any time, including from an atexit handler.
void number_stats_dump(FILE *out);
#endif

#endif // NUMBER_H
