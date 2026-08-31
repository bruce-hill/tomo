// Tests for the number library (src/stdlib/number.c).
// Build & run: make test-number (compiles with ASan/UBSan).

#include "number.h"

#include <float.h>
#include <gc.h>
#include <gmp.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            exit(1);                                                                                                   \
        }                                                                                                              \
        checks++;                                                                                                      \
    } while (0)

// want_exact: 1/0 to check the exactness flag, -1 to ignore it.
static void expect_str(Num_t x, uint32_t digits, const char *want, int want_exact) {
    bool exact;
    char *s = number_to_string(x, digits, &exact);
    if (strcmp(s, want) != 0) {
        fprintf(stderr, "FAIL: to_string gave \"%s\", want \"%s\"\n", s, want);
        exit(1);
    }
    if (want_exact >= 0 && exact != (want_exact != 0)) {
        fprintf(stderr, "FAIL: to_string(\"%s\") exact=%d, want %d\n", s, exact, want_exact);
        exit(1);
    }
    checks++;
}

static void expect_sym(Num_t x, const char *want) {
    char *s = number_to_symbolic(x);
    if (strcmp(s, want) != 0) {
        fprintf(stderr, "FAIL: to_symbolic gave \"%s\", want \"%s\"\n", s, want);
        exit(1);
    }
    checks++;
}

static void expect_tex(Num_t x, const char *want) {
    char *s = number_to_tex(x);
    if (strcmp(s, want) != 0) {
        fprintf(stderr, "FAIL: to_tex gave \"%s\", want \"%s\"\n", s, want);
        exit(1);
    }
    checks++;
}

static void expect_eq(Num_t a, Num_t b) {
    CHECK(number_equal(a, b));
    CHECK(number_compare(a, b) == 0);
}

static void expect_error(Num_t x) {
    CHECK(number_is_error(x));
}

// Like expect_eq, but treats "both sides are the error
// value" as a pass too: number_equal (which expect_eq uses) documents the
// error value as never equal to anything, including another error --
// correct in general (e.g. 1/0 != 1/0's caller shouldn't get "equal" back
// by accident), but exactly wrong for cross-checking two independently
// -computed error results (e.g. a fast-path dispatcher and its general
// fallback both correctly rejecting the same division by zero) that
// legitimately agree.
static void expect_dispatch_eq(Num_t a, Num_t b) {
    if (number_is_error(a) && number_is_error(b)) {
        checks++;
        return;
    }
    expect_eq(a, b);
}

static void expect_msg(Num_t x, const char *want) {
    CHECK(number_is_error(x));
    const char *msg = number_error_message(x);
    if (!msg || strcmp(msg, want) != 0) {
        fprintf(stderr, "FAIL: error_message gave \"%s\", want \"%s\"\n", msg, want);
        exit(1);
    }
    checks++;
}

static void expect_roundtrip(double d) {
    Num_t x = number_from_double(d);
    double back = number_to_double(x);
    if (back != d) {
        fprintf(stderr, "FAIL: roundtrip %.17g -> %.17g\n", d, back);
        exit(1);
    }
    checks++;
}

static Num_t factorial(int n) {
    Num_t acc = number_from_int(1);
    for (int i = 2; i <= n; i++) {
        Num_t f = number_from_int(i);
        Num_t next = number_mul(acc, f);
        acc = next;
    }
    return acc;
}

// x is borrowed.
static uint32_t tag_of(Num_t x) {
    return (uint32_t)(x.bits & 3);
}

// What tomo_init() does for a real Tomo program: hand GMP the collector, so
// its limb buffers are GC-owned like everything else here.
static void *gc_gmp_alloc(size_t size) {
    return GC_MALLOC_ATOMIC(size);
}
static void *gc_gmp_realloc(void *ptr, size_t old_size, size_t new_size) {
    (void)old_size;
    return GC_REALLOC(ptr, new_size);
}
static void gc_gmp_free(void *ptr, size_t size) {
    (void)ptr, (void)size;
}

int main(void) {
    GC_INIT();
    mp_set_memory_functions(gc_gmp_alloc, gc_gmp_realloc, gc_gmp_free);

    // --- Basic integer arithmetic (integer sub-path) ---
    {
        Num_t two = number_from_int(2);
        Num_t four = number_add(two, two);
        expect_str((four), 5, "4", 1);
        CHECK(tag_of(four) == 1); // still a small immediate
        expect_str(number_from_int(0), 5, "0", 1);
        expect_str(number_from_int(-17), 0, "-17", 1);
    }

    // --- NUMBER_SMALL: the compile-time-literal packing macro (number.h)
    // must produce bit-identical output to the runtime small-rational
    // packing (number_from_ratio, for an already-reduced in-bounds pair) --
    // this is the contract a code generator emitting NUMBER_SMALL(...)
    // literals is trusting; a future change to the internal representation
    // must not silently break it. ---
    {
        int64_t nums[] = {0,
                          1,
                          -1,
                          42,
                          -42,
                          1,
                          -1,
                          1,
                          22,
                          -22,
                          NUMBER_SMALL_NUM_MAX,
                          NUMBER_SMALL_NUM_MIN,
                          NUMBER_SMALL_NUM_MAX,
                          NUMBER_SMALL_NUM_MIN,
                          1};
        uint32_t dens[] = {
            1, 1, 1, 1, 1, 2, 2, 3, 7, 7, 1, 1, NUMBER_SMALL_DEN_MAX, NUMBER_SMALL_DEN_MAX, NUMBER_SMALL_DEN_MAX};
        for (size_t i = 0; i < sizeof(nums) / sizeof(*nums); i++) {
            Num_t viaMacro = NUMBER_SMALL(nums[i], dens[i]);
            Num_t viaRuntime = number_from_ratio(nums[i], (int64_t)dens[i]);
            CHECK(viaMacro.bits == viaRuntime.bits);
            CHECK(tag_of(viaMacro) == 1); // genuinely the small-immediate tier, not a promoted bigrat
        }
    }

    // --- Fast-path dispatcher vs. general implementation: number_add et
    // al. (number.h) are a forced-inline fast path (both operands already
    // tier-1, no overflow) falling through to number_*_general (number.c)
    // for everything else. This cross-checks the two against each other --
    // via expect_eq (number_equal), not raw .bits: a result that correctly
    // overflows out of tier 1 is a bigrat, and two independently-allocated
    // bigrats representing the same value are equal but never bit-identical
    // (different heap addresses). The spread is deliberately aimed at
    // every edge the fast path itself branches on: the integer sub-path
    // (both denominators 1) vs. general small fractions (falls through),
    // overflow right at the NUMBER_SMALL_NUM_MAX/MIN boundary (falls
    // through), values already promoted to bigrat (falls through
    // immediately on the tag check), and, for division specifically,
    // both the evenly-divides fast path and inexact division and division
    // by zero (both fall through). A future change to either
    // implementation that desyncs them fails here, not silently in a
    // caller. ---
    {
        int64_t small_ints[] = {0,
                                1,
                                -1,
                                42,
                                -42,
                                1000,
                                -1000,
                                NUMBER_SMALL_NUM_MAX,
                                NUMBER_SMALL_NUM_MIN,
                                NUMBER_SMALL_NUM_MAX - 1,
                                NUMBER_SMALL_NUM_MIN + 1};
        size_t n_ints = sizeof(small_ints) / sizeof(*small_ints);
        for (size_t i = 0; i < n_ints; i++) {
            for (size_t j = 0; j < n_ints; j++) {
                Num_t a1 = number_from_int(small_ints[i]), a2 = number_from_int(small_ints[i]);
                Num_t b1 = number_from_int(small_ints[j]), b2 = number_from_int(small_ints[j]);
                expect_dispatch_eq(number_add(a1, b1), number_add_general(a2, b2));
                a1 = number_from_int(small_ints[i]);
                a2 = number_from_int(small_ints[i]);
                b1 = number_from_int(small_ints[j]);
                b2 = number_from_int(small_ints[j]);
                expect_dispatch_eq(number_sub(a1, b1), number_sub_general(a2, b2));
                a1 = number_from_int(small_ints[i]);
                a2 = number_from_int(small_ints[i]);
                b1 = number_from_int(small_ints[j]);
                b2 = number_from_int(small_ints[j]);
                expect_dispatch_eq(number_mul(a1, b1), number_mul_general(a2, b2));
                a1 = number_from_int(small_ints[i]);
                a2 = number_from_int(small_ints[i]);
                b1 = number_from_int(small_ints[j]);
                b2 = number_from_int(small_ints[j]);
                expect_dispatch_eq(number_div(a1, b1), number_div_general(a2, b2)); // incl. b==0
            }
            expect_dispatch_eq(number_neg(number_from_int(small_ints[i])),
                               number_neg_general(number_from_int(small_ints[i])));
        }

        // Small fractions: both TAG_SMALL, but not the integer sub-path --
        // exercises the fast dispatcher's "falls through because
        // denominator != 1" branch specifically.
        int64_t frac_nums[] = {1, -1, 22, -22, 355, -355};
        int64_t frac_dens[] = {2, 3, 7, 113, NUMBER_SMALL_DEN_MAX};
        size_t n_frac_nums = sizeof(frac_nums) / sizeof(*frac_nums);
        for (size_t i = 0; i < n_frac_nums; i++) {
            for (size_t j = 0; j < sizeof(frac_dens) / sizeof(*frac_dens); j++) {
                int64_t an = frac_nums[i], ad = frac_dens[j];
                int64_t bn = frac_nums[(i + 1) % n_frac_nums], bd = frac_dens[j];
                // Operands are borrowed (see number.h), so each pair is
                // constructed fresh, borrowed by both the fast and general
                // call, then dropped explicitly. Unlike expect_eq's
                // arguments (its *results*), a borrowed operand is never
                // implicitly freed by the call it's passed to.
                Num_t a1 = number_from_ratio(an, ad), a2 = number_from_ratio(an, ad);
                Num_t b1 = number_from_ratio(bn, bd), b2 = number_from_ratio(bn, bd);
                expect_dispatch_eq(number_add(a1, b1), number_add_general(a2, b2));

                a1 = number_from_ratio(an, ad), a2 = number_from_ratio(an, ad);
                b1 = number_from_ratio(bn, bd), b2 = number_from_ratio(bn, bd);
                expect_dispatch_eq(number_sub(a1, b1), number_sub_general(a2, b2));

                a1 = number_from_ratio(an, ad), a2 = number_from_ratio(an, ad);
                b1 = number_from_ratio(bn, bd), b2 = number_from_ratio(bn, bd);
                expect_dispatch_eq(number_mul(a1, b1), number_mul_general(a2, b2));

                a1 = number_from_ratio(an, ad), a2 = number_from_ratio(an, ad);
                b1 = number_from_ratio(bn, bd), b2 = number_from_ratio(bn, bd);
                expect_dispatch_eq(number_div(a1, b1), number_div_general(a2, b2));
            }
        }

        // Already-promoted bigrat operands: the fast dispatcher's very
        // first check (both TAG_SMALL) must correctly and immediately fall
        // through, not misfire on a heap-pointer's low bits.
        {
            Num_t big1 = number_from_string("123456789012345678901234567890");
            Num_t big2 = number_from_string("123456789012345678901234567890");
            Num_t small1 = number_from_int(7), small2 = number_from_int(7);
            expect_dispatch_eq(number_add(big1, small1), number_add_general(big2, small2));

            big1 = number_from_string("123456789012345678901234567890");
            big2 = number_from_string("123456789012345678901234567890");
            expect_dispatch_eq(number_neg(big1), number_neg_general(big2));
        }
    }

    // --- Fast-path dispatcher vs. general implementation: number_compare
    // and number_equal (number.h) get the same forced-inline fast-path
    // treatment as the arithmetic operators above. Unlike those, these
    // return a plain int/bool, not a `number`, so this compares the
    // returned values directly with == (no bits-vs-pointer or
    // error-is-never-equal-to-another-error subtlety to work around here,
    // since neither is a `number` result). ---
    {
        int64_t cmp_ints[] = {0,
                              1,
                              -1,
                              42,
                              -42,
                              1000,
                              -1000,
                              NUMBER_SMALL_NUM_MAX,
                              NUMBER_SMALL_NUM_MIN,
                              NUMBER_SMALL_NUM_MAX - 1,
                              NUMBER_SMALL_NUM_MIN + 1};
        size_t n_cmp_ints = sizeof(cmp_ints) / sizeof(*cmp_ints);
        for (size_t i = 0; i < n_cmp_ints; i++) {
            for (size_t j = 0; j < n_cmp_ints; j++) {
                Num_t a1 = number_from_int(cmp_ints[i]), a2 = number_from_int(cmp_ints[i]);
                Num_t b1 = number_from_int(cmp_ints[j]), b2 = number_from_int(cmp_ints[j]);
                CHECK(number_compare(a1, b1) == number_compare_general(a2, b2));
                CHECK(number_equal(a1, b1) == number_equal_general(a2, b2));
            }
        }

        // Small fractions: both TAG_SMALL, exercises the cross-multiply
        // path (number_compare) and the bit-equality-suffices path
        // (number_equal).
        int64_t cmp_frac_nums[] = {1, -1, 22, -22, 355, -355};
        int64_t cmp_frac_dens[] = {2, 3, 7, 113, NUMBER_SMALL_DEN_MAX};
        size_t n_cmp_frac_nums = sizeof(cmp_frac_nums) / sizeof(*cmp_frac_nums);
        for (size_t i = 0; i < n_cmp_frac_nums; i++) {
            for (size_t j = 0; j < sizeof(cmp_frac_dens) / sizeof(*cmp_frac_dens); j++) {
                int64_t an = cmp_frac_nums[i], ad = cmp_frac_dens[j];
                int64_t bn = cmp_frac_nums[(i + 1) % n_cmp_frac_nums], bd = cmp_frac_dens[j];
                Num_t a1 = number_from_ratio(an, ad), a2 = number_from_ratio(an, ad);
                Num_t b1 = number_from_ratio(bn, bd), b2 = number_from_ratio(bn, bd);
                CHECK(number_compare(a1, b1) == number_compare_general(a2, b2));
                CHECK(number_equal(a1, b1) == number_equal_general(a2, b2));
                // Also against itself: an equal-but-distinct small rational
                // (same i, both operands from frac_nums[i]) exercises the
                // fast path's own a.bits == b.bits shortcut specifically.
                Num_t c1 = number_from_ratio(an, ad), c2 = number_from_ratio(an, ad);
                Num_t d1 = number_from_ratio(an, ad), d2 = number_from_ratio(an, ad);
                CHECK(number_compare(c1, d1) == number_compare_general(c2, d2));
                CHECK(number_equal(c1, d1) == number_equal_general(c2, d2));
            }
        }

        // Already-promoted bigrat and mixed-tier operands, plus an actual
        // shared heap reference (retain, not a fresh equal construction):
        // the fast dispatcher's outer TAG_SMALL check must correctly and
        // immediately fall through in every case, leaving
        // number_*_general's own same-pointer/same-tier logic untouched.
        {
            Num_t big1 = number_from_string("123456789012345678901234567890");
            Num_t big2 = number_from_string("123456789012345678901234567890");
            Num_t small1 = number_from_int(7), small2 = number_from_int(7);
            CHECK(number_compare(big1, small1) == number_compare_general(big2, small2));
            CHECK(number_equal(big1, small1) == number_equal_general(big2, small2));

            Num_t shared = number_from_string("123456789012345678901234567890");
            Num_t shared_ref = (shared);
            CHECK(number_compare(shared, shared_ref) == 0);
            CHECK(number_equal(shared, shared_ref) == true);
        }

        // Error values: TAG_ERROR never matches the fast path's TAG_SMALL
        // check, so both sides always fall through to _general. This
        // confirms that fallthrough itself stays correct, not new fast-path
        // logic.
        {
            Num_t e1 = number_from_ratio(1, 0), e2 = number_from_ratio(1, 0); // div by zero -> error
            Num_t seven1 = number_from_int(7), seven2 = number_from_int(7);
            CHECK(number_compare(e1, seven1) == number_compare_general(e2, seven2));
            CHECK(number_equal(e1, seven1) == number_equal_general(e2, seven2));
        }
    }

    // --- Fractions, reduction, canonical equality ---
    {
        Num_t third = number_from_ratio(1, 3);
        Num_t sixth = number_from_ratio(1, 6);
        Num_t sum = number_add(third, sixth);
        expect_eq((sum), number_from_ratio(1, 2));
        expect_str(sum, 10, "0.5", 1);

        expect_eq(number_from_ratio(2, 4), number_from_ratio(1, 2));
        expect_eq(number_from_ratio(100, -300), number_from_ratio(-1, 3));
        expect_str(number_from_ratio(-1, 3), 3, "-0.333", 0);
        expect_str(number_from_ratio(22, 7), 5, "3.14286", 0);
    }

    // --- Promotion to big rationals and demotion back ---
    {
        Num_t m = number_from_int(1 << 20);
        Num_t big = number_mul(m, m); // 2^40: overflows the small form
        CHECK(tag_of(big) == 0);
        expect_str((big), 0, "1099511627776", 1);
        Num_t back = number_div(big, m); // demotes to small again
        CHECK(tag_of(back) == 1);
        expect_eq(back, (m));

        Num_t f30 = factorial(30);
        CHECK(tag_of(f30) == 0);
        expect_str((f30), 0, "265252859812191058636308480000000", 1);
        Num_t f29 = factorial(29);
        Num_t ratio = number_div(f30, f29);
        CHECK(tag_of(ratio) == 1);
        expect_eq(ratio, number_from_int(30));
    }

    // --- Exact conversion from double ---
    {
        expect_eq(number_from_double(0.5), number_from_ratio(1, 2));
        expect_eq(number_from_double(-0.75), number_from_ratio(-3, 4));

        // 0.1 is not exactly 1/10 as a double; check its exact expansion.
        Num_t tenth = number_from_double(0.1);
        bool exact;
        char *s = number_to_string(tenth, 60, &exact);
        CHECK(exact);
        CHECK(strlen(s) == 57); // "0." + 55 exact fractional digits
        CHECK(strncmp(s, "0.1000000000000000055511", 24) == 0);
        Num_t reparsed = number_from_string(s);
        expect_eq(reparsed, (tenth));

        expect_error(number_from_double(NAN));
        expect_error(number_from_double(INFINITY));
    }

    // --- Correctly rounded conversion to double ---
    {
        expect_roundtrip(0.0);
        expect_roundtrip(0.1);
        expect_roundtrip(1.0 / 3.0);
        expect_roundtrip(3.141592653589793);
        expect_roundtrip(-2.5);
        expect_roundtrip(123456789.987654321);
        expect_roundtrip(1e300);
        expect_roundtrip(1e-300);
        expect_roundtrip(DBL_MAX);
        expect_roundtrip(DBL_MIN);
        expect_roundtrip(5e-324); // smallest subnormal

        Num_t third = number_from_ratio(1, 3);
        CHECK(number_to_double(third) == 1.0 / 3.0);

        // Big-rational paths (values that can't be small rationals):
        Num_t x = number_from_string("1e300");
        CHECK(number_to_double(x) == 1e300);
        x = number_from_string("1/100000000000000000000");
        CHECK(number_to_double(x) == 1e-20);
        x = number_from_string("123456789012345678901234567890.5");
        CHECK(number_to_double(x) == 123456789012345678901234567890.5);
        x = number_from_string("5e-324"); // decimal -> nearest subnormal
        CHECK(number_to_double(x) == 5e-324);
        x = number_from_string("1e400"); // overflows to infinity
        CHECK(isinf(number_to_double(x)) && number_to_double(x) > 0);
        x = number_from_string("-1e400");
        CHECK(isinf(number_to_double(x)) && number_to_double(x) < 0);
        x = number_from_string("1e-400"); // underflows to zero
        CHECK(number_to_double(x) == 0.0);
    }

    // --- Round-half-even in decimal output ---
    {
        expect_str(number_from_ratio(1, 8), 2, "0.12", 0); // 0.125: tie, 2 even
        expect_str(number_from_ratio(3, 8), 2, "0.38", 0); // 0.375: tie, 7 odd
        expect_str(number_from_ratio(7, 2), 0, "4", 0); // 3.5 -> 4
        expect_str(number_from_ratio(5, 2), 0, "2", 0); // 2.5 -> 2
        expect_str(number_from_ratio(1, 2), 0, "0", 0); // 0.5 -> 0
        expect_str(number_from_string("99.95"), 1, "100.0", 0); // carry cascade
    }

    // --- Parsing ---
    {
        expect_eq(number_from_string("22/7"), number_from_ratio(22, 7));
        expect_eq(number_from_string("0.5"), number_from_ratio(1, 2));
        expect_eq(number_from_string("-3.5"), number_from_ratio(-7, 2));
        expect_eq(number_from_string("  -3.5  "), number_from_ratio(-7, 2));
        expect_eq(number_from_string("3."), number_from_int(3));
        expect_eq(number_from_string("2.5e1"), number_from_int(25));
        expect_eq(number_from_string("25e-1"), number_from_ratio(5, 2));
        expect_str(number_from_string("6.02e23"), 0, "602000000000000000000000", 1);

        expect_error(number_from_string(""));
        expect_error(number_from_string("abc"));
        expect_error(number_from_string("1e"));
        expect_error(number_from_string("1/"));
        expect_error(number_from_string("1/0"));
        expect_error(number_from_string("."));
        expect_error(number_from_string("1.5x"));
        expect_error(number_from_string(NULL));
    }

    // --- number_from_decimal: the exact-literal constructor ---
    {
        // The headline guarantee: "3.15" is 63/20 exactly, NOT the nearest
        // double number_from_double(3.15) would hand back.
        expect_sym(number_from_decimal("3.15"), "63/20");
        {
            Num_t exact = number_from_decimal("3.15");
            Num_t viadbl = number_from_double(3.15);
            CHECK(!number_equal(exact, viadbl)); // the whole point: they differ
        }
        // Same decimal/scientific grammar as number_from_string, all exact.
        expect_eq(number_from_decimal("0.5"), number_from_ratio(1, 2));
        expect_eq(number_from_decimal("  -3.5  "), number_from_ratio(-7, 2));
        expect_eq(number_from_decimal("25e-1"), number_from_ratio(5, 2));
        expect_str(number_from_decimal("6.02e23"), 0, "602000000000000000000000", 1);
        // The ratio form is rejected here (not decimal notation).
        expect_error(number_from_decimal("22/7"));
        expect_error(number_from_decimal("1/0"));
        expect_error(number_from_decimal("abc"));
        expect_error(number_from_decimal(NULL));
    }

    // --- Comparison ---
    {
        Num_t third = number_from_ratio(1, 3);
        Num_t approx = number_from_string("0.333");
        CHECK(number_compare(third, approx) > 0);
        CHECK(number_compare(approx, third) < 0);

        Num_t a = number_from_int(-5), b = number_from_int(3);
        CHECK(number_compare(a, b) < 0);

        Num_t f25 = factorial(25), f26 = factorial(26);
        CHECK(number_compare(f25, f26) < 0);
        CHECK(number_compare(f26, f25) > 0);

        // Irrational vs. rational: decided exactly by interval refinement
        // (never the "unordered" 2), and antisymmetric. This is the case
        // calc's `pi > 3.1` comparisons lean on.
        Num_t pi = number_pi();
        Num_t r31 = number_from_string("3.1");
        CHECK(number_compare(pi, r31) > 0);
        CHECK(number_compare(r31, pi) < 0);
        CHECK(!number_equal(pi, r31));

        // Identical radicals are recognized as equal symbolically, so
        // number_compare returns 0 and number_equal is true even across the
        // two different representations; a rational still orders strictly against them.
        Num_t s2 = number_sqrt(number_from_int(2));
        Num_t twos2 = number_mul(number_from_int(2), s2);
        Num_t s8 = number_sqrt(number_from_int(8)); // == 2*sqrt(2)
        CHECK(number_compare(s8, twos2) == 0);
        CHECK(number_equal(s8, twos2));
        CHECK(number_compare(s8, number_from_int(2)) > 0); // sqrt(8) ~ 2.83

        // The one genuinely undecidable case: a general (non-symbolic-form)
        // irrational equal to a rational with no identity the library
        // recognizes to prove it. Rather than loop forever, both give up past
        // the precision cap: number_compare reports 2 ("unordered") and
        // number_equal reports false. calc's inexact fallback relies on
        // exactly this contract (see calc.c).
        Num_t one = number_from_int(1);
        Num_t sin1 = number_sin(one), cos1 = number_cos(one);
        Num_t sin1sq = number_mul(sin1, sin1), cos1sq = number_mul(cos1, cos1);
        Num_t pyth = number_add(sin1sq, cos1sq); // sin(1)^2 + cos(1)^2, == 1
        CHECK(number_compare(pyth, one) == 2);
        CHECK(!number_equal(pyth, one));
    }

    // --- Unary operations ---
    {
        expect_error(number_neg(NUMBER_ERROR));
        expect_error(number_abs(NUMBER_ERROR));
        Num_t x = number_from_ratio(-2, 3);
        expect_eq(number_neg(x), number_from_ratio(2, 3));
        expect_eq(number_abs(x), number_from_ratio(2, 3));
        expect_eq(number_inverse(x), number_from_ratio(-3, 2));
        CHECK(number_is_negative(x));
        CHECK(number_sign(x) == -1);

        Num_t big = factorial(30);
        Num_t negbig = number_neg(big);
        CHECK(number_sign(negbig) == -1);
        expect_eq(number_abs(negbig), (big));
        Num_t invbig = number_inverse(big);
        Num_t roundtrip = number_inverse(invbig);
        expect_eq(roundtrip, (big));

        expect_error(number_inverse(number_from_int(0)));
    }

    // --- Errors and propagation ---
    {
        expect_error(number_from_ratio(1, 0));
        Num_t one = number_from_int(1), zero = number_from_int(0);
        expect_error(number_div(one, zero));
        expect_error(number_add(NUMBER_ERROR, one));
        expect_error(number_mul(one, NUMBER_ERROR));

        CHECK(isnan(number_to_double(NUMBER_ERROR)));
        CHECK(number_compare(NUMBER_ERROR, NUMBER_ERROR) == 2);
        CHECK(!number_equal(NUMBER_ERROR, NUMBER_ERROR));
        expect_str(NUMBER_ERROR, 5, "(error)", -1);
    }

    // --- Error messages ---
    {
        CHECK(number_error_message(number_from_int(5)) == NULL);
        CHECK(strcmp(number_error_message(NUMBER_ERROR), "undefined result") == 0);

        expect_msg(number_from_ratio(1, 0), "division by zero");
        expect_msg(number_div(number_from_int(1), number_from_int(0)), "division by zero");
        expect_msg(number_inverse(number_from_int(0)), "division by zero");
        expect_msg(number_sqrt(number_from_int(-1)), "square root of a negative number");
        {
            Num_t pi = number_pi();
            Num_t neg_pi = number_neg(pi);
            expect_msg(number_sqrt(neg_pi), "square root of a negative number");
        }
        expect_msg(number_ln(number_from_int(0)), "logarithm of a non-positive number");
        expect_msg(number_ln(number_from_int(-5)), "logarithm of a non-positive number");
        expect_msg(number_log10(number_from_int(-1)), "logarithm of a non-positive number");
        expect_msg(number_asin(number_from_int(2)), "arcsine or arccosine of a value outside [-1, 1]");
        expect_msg(number_acos(number_from_ratio(-3, 2)), "arcsine or arccosine of a value outside [-1, 1]");
        expect_msg(number_pow(number_from_int(0), number_from_int(-1)), "zero raised to a negative power");
        expect_msg(number_pow(number_from_int(-2), number_from_ratio(1, 2)), "square root of a negative number");
        expect_msg(number_pow(number_from_int(-2), number_from_ratio(1, 3)),
                   "a negative number raised to a non-integer power (not a real result)");
        expect_msg(number_from_string("not a number"), "invalid number syntax");
        expect_msg(number_from_string("1/"), "invalid number syntax");
        expect_msg(number_from_double(NAN), "not a finite number (NaN or infinity)");
        expect_msg(number_from_double(INFINITY), "not a finite number (NaN or infinity)");
        {
            Num_t pi2 = number_pi(), two = number_from_int(2);
            Num_t half_pi = number_div(pi2, two);
            expect_msg(number_tan(half_pi), "division by zero"); // cos(pi/2) == 0 exactly
        }

        // Propagation carries the original reason forward through further
        // arithmetic, rather than resetting to a generic message.
        {
            Num_t bad = number_div(number_from_int(1), number_from_int(0));
            Num_t five = number_from_int(5);
            expect_msg(number_mul(bad, five), "division by zero");
        }

        // Two colliding errors: either message is an acceptable answer, but
        // it must be one of the two specific reasons, not a generic fallback.
        {
            Num_t a = number_div(number_from_int(2), number_from_int(0));
            Num_t b = number_sqrt(number_from_int(-1));
            Num_t sum = number_add(a, b);
            const char *msg = number_error_message(sum);
            CHECK(strcmp(msg, "division by zero") == 0 || strcmp(msg, "square root of a negative number") == 0);
        }
    }

    // --- Tier 3: sqrt ---
    {
        expect_eq(number_sqrt(number_from_int(4)), number_from_int(2));
        expect_eq(number_sqrt(number_from_ratio(9, 4)), number_from_ratio(3, 2));
        expect_eq(number_sqrt(number_from_int(0)), number_from_int(0));

        Num_t two = number_from_int(2);
        Num_t s2 = number_sqrt(two);
        CHECK(!number_is_rational(s2));
        expect_str((s2), 10, "1.4142135624", 0);
        CHECK(number_to_double(s2) == 1.4142135623730951);

        // The design's marquee identity: sqrt(2)*sqrt(2) == 2, exactly.
        Num_t prod = number_mul(s2, s2);
        CHECK(number_is_rational(prod));
        expect_eq(prod, (two));

        // sqrt(8) == 2*sqrt(2): commensurable radicands are recognized.
        Num_t s8 = number_sqrt(number_from_int(8));
        Num_t twice_s2 = number_mul(two, s2);
        CHECK(number_equal(s8, twice_s2));
        Num_t q = number_div(s8, s2);
        expect_eq(q, (two));
        Num_t three = number_from_int(3);
        Num_t sum = number_add(s8, s2); // sqrt(8) + sqrt(2) == 3*sqrt(2)
        Num_t three_s2 = number_mul(three, s2);
        CHECK(number_equal(sum, three_s2));

        // Field arithmetic in Q(sqrt 2): (1+sqrt2)^2 = 3+2sqrt2,
        // and 1/(1+sqrt2) = sqrt2 - 1.
        Num_t one = number_from_int(1);
        Num_t onePlus = number_add(one, s2);
        Num_t sq = number_mul(onePlus, onePlus);
        Num_t three2 = number_from_int(3);
        Num_t two_s2 = number_mul(two, s2);
        Num_t expected = number_add(three2, two_s2);
        CHECK(number_equal(sq, expected));
        Num_t inv = number_inverse(onePlus);
        Num_t s2m1 = number_sub(s2, one);
        CHECK(number_equal(inv, s2m1));

        // sqrt(3)/sqrt(27) == 1/3 and sqrt(2)*sqrt(3) == sqrt(6)
        Num_t s3 = number_sqrt(number_from_int(3));
        Num_t s27 = number_sqrt(number_from_int(27));
        Num_t third = number_div(s3, s27);
        expect_eq(third, number_from_ratio(1, 3));
        Num_t s6a = number_mul(s2, s3);
        Num_t s6b = number_sqrt(number_from_int(6));
        CHECK(number_equal(s6a, s6b));

        // Incommensurable sums fall back to the general IRRATIONAL engine.
        Num_t sum23 = number_add(s2, s3);
        CHECK(!number_is_rational(sum23));
        expect_str(sum23, 10, "3.1462643699", 0);
        // Genuine domain errors are unaffected by the general fallback.
        expect_error(number_sqrt(number_from_int(-1)));

        // Nested sqrt now evaluates via the general fallback instead of
        // erroring: sqrt(sqrt(2)) == 2^(1/4).
        Num_t nested = number_sqrt(s2); // borrows s2
        CHECK(!number_is_rational(nested));
        expect_str(nested, 20, "1.18920711500272106672", 0);
        // A negative real (still no closed sqrt form, still a domain error).
        Num_t neg_s2 = number_neg(s2);
        expect_error(number_sqrt(neg_s2));

        CHECK(number_compare(s2, two) < 0);
        Num_t one_and_half = number_from_ratio(3, 2);
        CHECK(number_compare(s2, one_and_half) < 0);
        CHECK(number_compare(one_and_half, s2) > 0);
    }

    // --- Tier 3: pi ---
    {
        Num_t pi = number_pi();
        CHECK(!number_is_rational(pi));
        expect_str((pi), 10, "3.1415926536", 0);
        expect_str((pi), 4, "3.1416", 0);
        CHECK(number_to_double(pi) == 3.141592653589793);

        Num_t diff = number_sub(pi, pi); // pi - pi == 0, exactly rational
        CHECK(number_is_rational(diff));
        expect_eq(diff, number_from_int(0));

        Num_t two = number_from_int(2);
        Num_t twopi = number_mul(two, pi);
        Num_t sum = number_add(pi, pi);
        CHECK(number_equal(sum, twopi));
        expect_str((twopi), 6, "6.283185", 0);
        Num_t half = number_div(twopi, two);
        CHECK(number_equal(half, pi));

        Num_t negpi = number_neg(pi);
        CHECK(number_sign(negpi) == -1);
        expect_str((negpi), 5, "-3.14159", 0);
        Num_t abspi = number_abs(negpi);
        CHECK(number_equal(abspi, pi));

        // pi vs. rationals and algebraic numbers: exact comparisons.
        Num_t approx = number_from_ratio(22, 7);
        CHECK(number_compare(pi, approx) < 0); // pi < 22/7
        CHECK(number_compare(approx, pi) > 0);
        Num_t s2 = number_sqrt(two);
        CHECK(number_compare(s2, pi) < 0); // via interval refinement

        // Outside the closed symbolic forms: the general IRRATIONAL engine.
        Num_t pi_plus_s2 = number_add(pi, s2);
        CHECK(!number_is_rational(pi_plus_s2));
        expect_str(pi_plus_s2, 10, "4.5558062160", 0);
        Num_t pi_sq = number_mul(pi, pi);
        CHECK(!number_is_rational(pi_sq));
        expect_str(pi_sq, 10, "9.8696044011", 0);
        Num_t inv_pi = number_inverse(pi);
        CHECK(!number_is_rational(inv_pi));
        expect_str(inv_pi, 10, "0.3183098862", 0);
    }

    // --- Named constants: NUMBER_ZERO/ONE/NEG_ONE, number_tau, number_sqrt2 ---
    {
        Num_t z = number_from_int(0), o = number_from_int(1), negone = number_from_int(-1);
        CHECK(NUMBER_ZERO.bits == z.bits);
        CHECK(NUMBER_ONE.bits == o.bits);
        CHECK(NUMBER_NEG_ONE.bits == negone.bits);

        // number_pi/number_tau/number_sqrt2 are cached singletons (number.c):
        // repeated calls, retains, and drops of the *same* underlying heap
        // object must never leave a dangling reference or corrupt the cache
        // for the next call. Exercise that cycle hard, since ASan/UBSan (this
        // build) catch any use-after-free or leak here.
        for (int i = 0; i < 50; i++) {
            Num_t a = number_pi();
            Num_t b = number_pi();
            CHECK(number_equal(a, b)); // same cached value every time
        }

        Num_t pi = number_pi();
        Num_t tau = number_tau();
        CHECK(!number_is_rational(tau));
        Num_t two_pi = number_mul(number_from_int(2), pi);
        CHECK(number_equal(tau, two_pi)); // tau == 2*pi, exactly
        expect_str((tau), 10, "6.2831853072", 0);

        Num_t sq2a = number_sqrt2();
        Num_t sq2b = number_sqrt(number_from_int(2));
        CHECK(number_equal(sq2a, sq2b));
        Num_t prod = number_mul(sq2a, sq2a);
        CHECK(number_is_rational(prod)); // sqrt(2)*sqrt(2) == 2, exactly
        expect_eq(prod, number_from_int(2));

        // Precision refined by one call must still be correct when read
        // back from an entirely separate later call to the same constant.
        Num_t pi_hi = number_pi();
        char *s = number_to_string(pi_hi, 30, NULL);
        // Correctly rounded (not truncated) at digit 30: pi's 31st digit is
        // 5 followed by more nonzero digits, so ...383279|50288... rounds
        // ...79 up to ...80.
        CHECK(strncmp(s, "3.141592653589793238462643383280", 32) == 0);
    }

    // --- Tier 3: general engine (sin/cos/tan/asin/acos/atan/exp/ln/log10/
    // sinh/cosh/tanh/pow) ---
    {
        // Rational escapes: exact, no IRRATIONAL node built at all.
        expect_eq(number_sin(number_from_int(0)), number_from_int(0));
        expect_eq(number_cos(number_from_int(0)), number_from_int(1));
        expect_eq(number_tan(number_from_int(0)), number_from_int(0));
        expect_eq(number_atan(number_from_int(0)), number_from_int(0));
        expect_eq(number_asin(number_from_int(0)), number_from_int(0));
        expect_eq(number_exp(number_from_int(0)), number_from_int(1));
        expect_eq(number_ln(number_from_int(1)), number_from_int(0));
        expect_eq(number_log10(number_from_int(1)), number_from_int(0));
        // log10 is exact for any integer power of ten, positive or negative
        // exponent, not just x == 1. log10(1000) used to print as
        // "3.0000000000" (numerically correct but not recognized as exact);
        // it's exactly 3 now.
        expect_eq(number_log10(number_from_int(1000)), number_from_int(3));
        expect_eq(number_log10(number_from_int(100)), number_from_int(2));
        expect_eq(number_log10(number_from_ratio(1, 100)), number_from_int(-2));
        {
            Num_t n2 = number_log10(number_from_int(2));
            CHECK(!number_is_rational(n2)); // not a power of ten: general engine
        }

        // --- LN(r)/EXP(r) closed symbolic forms ---
        {
            // exp(ln(r)) == r and ln(exp(r)) == r exactly, for a rational r,
            // with no interval refinement needed, and the results collapse
            // straight back to the original rational.
            Num_t three = number_from_int(3);
            Num_t ln3 = number_ln(three);
            CHECK(!number_is_rational(ln3)); // ln(3) is genuinely irrational
            Num_t back = number_exp(ln3);
            expect_eq(back, (three));
            Num_t half = number_from_ratio(1, 2);
            Num_t exp_half = number_exp(half);
            CHECK(!number_is_rational(exp_half));
            Num_t back2 = number_ln(exp_half);
            expect_eq(back2, (half));

            // exp(a)*exp(b) == exp(a+b): a closed EXP(5), not an
            // unrecognized product of two EXP nodes.
            Num_t two = number_from_int(2);
            Num_t e2 = number_exp(two);
            Num_t e3 = number_exp(three);
            Num_t prod = number_mul(e2, e3);
            expect_sym((prod), "exp(5)");
            Num_t e5 = number_exp(number_from_int(5));
            expect_eq((prod), e5);

            // exp(a)/exp(b) == exp(a-b).
            Num_t e2b = number_exp(two);
            Num_t e3b = number_exp(three);
            Num_t quot = number_div(e2b, e3b);
            Num_t e_neg1 = number_exp(number_from_int(-1));
            expect_eq(quot, e_neg1);

            // ln(a) + ln(b) == ln(a*b): 2*ln(2) + ln(3) collapses to ln(12).
            Num_t ln2 = number_ln(two);
            Num_t two_ln2 = number_mul(two, ln2);
            Num_t ln3b = number_ln(three);
            Num_t sum = number_add(two_ln2, ln3b);
            expect_sym((sum), "ln(12)");
            Num_t ln12 = number_ln(number_from_int(12));
            expect_eq(sum, ln12);

            // 2*ln(2) - ln(4) == 0 exactly: integer-coefficient LN terms
            // with different (but related) arguments still combine.
            Num_t ln2b = number_ln(two);
            Num_t two_ln2b = number_mul(two, ln2b);
            Num_t ln4 = number_ln(number_from_int(4));
            expect_eq(number_sub(two_ln2b, ln4), number_from_int(0));

            // ln(1/2) == -ln(2): the < 1 canonicalization.
            Num_t ln_half = number_ln(number_from_ratio(1, 2));
            Num_t ln2c = number_ln(two);
            expect_eq(ln_half, number_neg(ln2c));

            // Non-integer-coefficient LN terms and mismatched-exponent EXP
            // terms don't unify: general fallback, still numerically
            // correct, not a rational.
            Num_t ln3c = number_ln(three);
            Num_t half_ln3 = number_mul(number_from_ratio(1, 2), ln3c);
            Num_t ln2d = number_ln(two);
            Num_t mixed = number_add(ln2d, half_ln3);
            CHECK(!number_is_rational(mixed));
            Num_t e2c = number_exp(two);
            Num_t e3c = number_exp(three);
            Num_t esum = number_add(e2c, e3c);
            CHECK(!number_is_rational(esum));
        }

        expect_eq(number_sinh(number_from_int(0)), number_from_int(0));
        expect_eq(number_cosh(number_from_int(0)), number_from_int(1));
        expect_eq(number_tanh(number_from_int(0)), number_from_int(0));
        expect_eq(number_pow(number_from_int(5), number_from_int(0)), number_from_int(1));
        expect_eq(number_pow(number_from_int(0), number_from_int(0)), number_from_int(1));
        expect_eq(number_pow(number_from_int(0), number_from_int(3)), number_from_int(0));
        expect_eq(number_pow(number_from_int(7), number_from_int(1)), number_from_int(7));

        // sin/cos of a rational multiple of pi with denominator 1 or 2 are
        // exact (the general engine alone could only ever approximate these
        // arbitrarily close to their true value, never prove them exactly
        // zero, e.g. sin(pi) would otherwise print as "0.000...0"). The
        // next block below covers denominators 3, 4, 6 (the sqrt-valued
        // special angles) plus confirming denominators outside {1,2,3,4,6}
        // correctly fall through to the general engine.
        {
            // None of sin/cos/tan consume their argument, so each of these
            // can be reused across multiple calls with no retain needed --
            // just one drop per value at the end.
            Num_t pi = number_pi(), two = number_from_int(2), three = number_from_int(3);
            Num_t half_pi = number_div(pi, two);
            Num_t neg_half_pi = number_neg(half_pi);
            Num_t two_pi = number_mul(two, pi);
            Num_t three_half_pi = number_mul(three, half_pi);

            expect_eq(number_sin(pi), number_from_int(0));
            expect_eq(number_cos(pi), number_from_int(-1));
            expect_eq(number_tan(pi), number_from_int(0));
            expect_eq(number_sin(half_pi), number_from_int(1));
            expect_eq(number_cos(half_pi), number_from_int(0));
            expect_eq(number_sin(neg_half_pi), number_from_int(-1));
            expect_eq(number_sin(two_pi), number_from_int(0));
            expect_eq(number_cos(two_pi), number_from_int(1));
            expect_eq(number_cos(three_half_pi), number_from_int(0));
            expect_error(number_tan(half_pi));
        }

        // Denominators 3, 4, 6: the sqrt(2)/2, sqrt(3)/2 special angles,
        // plus their reflections/rotations around the circle. number_div/
        // mul/neg all BORROW their arguments (see the Arithmetic section of
        // number.h), so every intermediate below is computed once, reused by
        // borrowing (sin/cos/tan/mul/neg never consume), and dropped
        // exactly once at the end; number_retain is only needed where the
        // SAME value is handed to expect_eq (which does consume) more than
        // once as the "expected" side.
        {
            Num_t half = number_from_ratio(1, 2);
            Num_t neg_half = number_neg(half);
            Num_t two = number_from_int(2);
            Num_t three = number_from_int(3);
            Num_t four = number_from_int(4);
            Num_t six = number_from_int(6);
            Num_t sq2 = number_sqrt(two);
            Num_t sqrt2_2 = number_div(sq2, two);
            Num_t sq3 = number_sqrt(three);
            Num_t sqrt3_2 = number_div(sq3, two);
            Num_t neg_sqrt2_2 = number_neg(sqrt2_2);
            Num_t neg_sqrt3_2 = number_neg(sqrt3_2);

            Num_t pi = number_pi();
            Num_t pi6 = number_div(pi, six);
            Num_t pi4 = number_div(pi, four);
            Num_t pi3 = number_div(pi, three);

            expect_eq(number_sin(pi6), (half));
            expect_eq(number_cos(pi6), (sqrt3_2));

            expect_eq(number_sin(pi4), (sqrt2_2));
            expect_eq(number_cos(pi4), (sqrt2_2));
            expect_eq(number_tan(pi4), number_from_int(1));

            expect_eq(number_sin(pi3), (sqrt3_2));
            expect_eq(number_cos(pi3), (half));
            expect_sym(number_tan(pi3), "sqrt(3)");

            // Reflections: 2pi/3, 3pi/4, 5pi/6, 7pi/6, 4pi/3, and a negative angle.
            Num_t two_pi3 = number_mul(two, pi3);
            expect_eq(number_sin(two_pi3), (sqrt3_2));
            expect_eq(number_cos(two_pi3), (neg_half));

            Num_t three_pi4 = number_mul(three, pi4);
            expect_eq(number_sin(three_pi4), (sqrt2_2));
            expect_eq(number_cos(three_pi4), (neg_sqrt2_2));

            Num_t five = number_from_int(5);
            Num_t five_pi6 = number_mul(five, pi6);
            expect_eq(number_sin(five_pi6), (half));
            expect_eq(number_cos(five_pi6), (neg_sqrt3_2));

            Num_t seven = number_from_int(7);
            Num_t seven_pi6 = number_mul(seven, pi6);
            expect_eq(number_sin(seven_pi6), (neg_half));

            Num_t four_pi3 = number_mul(four, pi3);
            expect_eq(number_cos(four_pi3), (neg_half));

            Num_t neg_pi6 = number_neg(pi6);
            expect_eq(number_sin(neg_pi6), (neg_half));
            Num_t neg_pi3 = number_neg(pi3);
            expect_eq(number_cos(neg_pi3), (half));
        }

        // Denominators outside {1,2,3,4,6} (needing pi/12's sqrt6+sqrt2 form,
        // or not a "nice" angle at all) fall through to the general engine:
        // not exact rationals/symbolic forms, but still numerically correct.
        {
            Num_t pi = number_pi();
            Num_t pi12 = number_div(pi, number_from_int(12));
            Num_t s1 = number_sin(pi12);
            CHECK(!number_is_rational(s1));
            expect_str(s1, 30, "0.258819045102520762348898837624", 0);
            Num_t pi5 = number_div(pi, number_from_int(5));
            Num_t s2 = number_sin(pi5);
            CHECK(!number_is_rational(s2));
            expect_str(s2, 30, "0.587785252292473129168705954639", 0);
        }

        // asin/acos(+-1) are exact via the closed pi/2 symbolic form, not
        // the general engine. number_is_rational is still false (they're
        // irrational), but the value carries no approximation error.
        Num_t asin1 = number_asin(number_from_int(1));
        CHECK(!number_is_rational(asin1));
        expect_sym(asin1, "pi/2");
        expect_sym(number_asin(number_from_int(-1)), "-pi/2");
        expect_str(number_acos(number_from_int(1)), 5, "0", 1);
        expect_sym(number_acos(number_from_int(-1)), "pi");
        expect_sym(number_acos(number_from_int(0)), "pi/2");

        // asin/atan/acos of the standard special ratios (mirroring the
        // sin/cos pi-multiple table above): 1/2, sqrt(2)/2, sqrt(3)/2 for
        // asin/acos; sqrt(3)/3, 1, sqrt(3) for atan.
        {
            Num_t half = number_from_ratio(1, 2);
            Num_t sq2 = number_sqrt(number_from_int(2));
            Num_t sqrt2_2 = number_div(sq2, number_from_int(2));
            Num_t sqrt3 = number_sqrt(number_from_int(3));
            Num_t sqrt3_2 = number_div(sqrt3, number_from_int(2));
            Num_t sqrt3_3 = number_div(sqrt3, number_from_int(3));

            expect_sym(number_asin(half), "pi/6");
            Num_t neg_half = number_neg(half);
            expect_sym(number_asin(neg_half), "-pi/6");
            expect_sym(number_asin(sqrt2_2), "pi/4");
            expect_sym(number_asin(sqrt3_2), "pi/3");

            expect_sym(number_atan(number_from_int(1)), "pi/4");
            expect_sym(number_atan(sqrt3), "pi/3");
            expect_sym(number_atan(sqrt3_3), "pi/6");
            Num_t neg_sqrt3 = number_neg(sqrt3);
            expect_sym(number_atan(neg_sqrt3), "-pi/3");

            expect_sym(number_acos(sqrt2_2), "pi/4");
            expect_sym(number_acos(half), "pi/3");

            // Not a special ratio: falls through to the general engine.
            Num_t a13 = number_asin(number_from_ratio(1, 3));
            CHECK(!number_is_rational(a13));
            expect_str(a13, 15, "0.339836909454122", 0);
        }

        // pow's exact ladder: integer exponents (any sign, any base) and
        // y=1/2 stay exact rationals/symbolic forms, never touching exp/ln.
        Num_t p1 = number_pow(number_from_int(2), number_from_int(10));
        CHECK(number_is_rational(p1));
        expect_str(p1, 5, "1024", 1);
        Num_t p2 = number_pow(number_from_int(2), number_from_int(-3));
        CHECK(number_is_rational(p2));
        expect_str(p2, 5, "0.125", 1);
        Num_t p3 = number_pow(number_from_int(-2), number_from_int(3));
        CHECK(number_is_rational(p3));
        expect_str(p3, 5, "-8", 1);
        expect_sym(number_pow(number_from_int(2), number_from_ratio(1, 2)), "sqrt(2)");

        // pow's exact ladder, continued: rational exponents p/q with q > 2
        // stay exact when x is itself a rational q-th power (numerator and
        // denominator each a perfect q-th power), e.g. 8^(1/3) == 2, not
        // an unsimplified exp(ln(8)/3). Odd q accepts a negative base (the
        // real q-th root exists); even q rejects it, same as sqrt.
        Num_t p4 = number_pow(number_from_int(8), number_from_ratio(1, 3));
        CHECK(number_is_rational(p4));
        expect_str(p4, 5, "2", 1);
        Num_t p5 = number_pow(number_from_int(27), number_from_ratio(2, 3));
        CHECK(number_is_rational(p5));
        expect_str(p5, 5, "9", 1);
        Num_t p6 = number_pow(number_from_int(4), number_from_ratio(3, 2));
        CHECK(number_is_rational(p6));
        expect_str(p6, 5, "8", 1);
        Num_t p7 = number_pow(number_from_int(-8), number_from_ratio(1, 3));
        CHECK(number_is_rational(p7));
        expect_str(p7, 5, "-2", 1);
        Num_t p8 = number_pow(number_from_int(-8), number_from_ratio(2, 3));
        CHECK(number_is_rational(p8));
        expect_str(p8, 5, "4", 1);
        Num_t p9 = number_pow(number_from_ratio(1, 8), number_from_ratio(1, 3));
        CHECK(number_is_rational(p9));
        expect_str(p9, 5, "0.5", 1);
        expect_error(number_pow(number_from_int(-4), number_from_ratio(1, 4)));
        // Not a perfect cube: falls through to the general engine, still
        // numerically correct, just not exact.
        Num_t p10 = number_pow(number_from_int(2), number_from_ratio(1, 3));
        CHECK(!number_is_rational(p10));
        expect_str(p10, 15, "1.259921049894873", 0);

        // Decimal digits against independently computed reference values
        // (mpmath, at 30+ digits): exercises the general engine's actual
        // precision budgets, not just "looks about right".
        expect_str(number_exp(number_from_int(1)), 15, "2.718281828459045", 0);
        expect_str(number_exp(number_from_int(-1)), 15, "0.367879441171442", 0);
        expect_str(number_ln(number_from_int(2)), 15, "0.693147180559945", 0);
        expect_str(number_ln(number_from_ratio(1, 2)), 15, "-0.693147180559945", 0);
        expect_str(number_log10(number_from_int(2)), 15, "0.301029995663981", 0);
        expect_str(number_sin(number_from_int(1)), 30, "0.841470984807896506652502321630", 0);
        expect_str(number_cos(number_from_int(1)), 30, "0.540302305868139717400936607443", 0);
        // Large-argument range reduction: pi needs boosted precision that
        // scales with the argument's own magnitude, not just the output
        // precision (see reduce_mod_2pi). This is the case that would
        // silently give the wrong low digits if that budget were wrong.
        expect_str(number_sin(number_from_int(1000000)), 30, "-0.349993502171292952117652486781", 0);
        expect_str(number_cos(number_from_int(1000000)), 30, "0.936752127533144786938532535075", 0);
        expect_str(number_tan(number_from_int(1)), 25, "1.5574077246549022305069748", 0);
        expect_str(number_atan(number_from_int(2)), 30, "1.107148717794090503017065460179", 0);
        expect_str(number_atan(number_from_int(10)), 30, // |x| > 1: reciprocal reduction
                   "1.471127674303734591852875571762", 0);
        expect_str(number_asin(number_from_ratio(1, 2)), 30, "0.523598775598298873077107230547", 0);
        expect_str(number_acos(number_from_ratio(1, 2)), 30, "1.047197551196597746154214461093", 0);
        expect_str(number_sinh(number_from_int(1)), 30, "1.175201193643801456882381850596", 0);
        expect_str(number_cosh(number_from_ratio(1, 2)), 30, "1.127625965206380785226225161403", 0);
        expect_str(number_tanh(number_from_int(1)), 30, "0.761594155955764888119458282605", 0);
        expect_str(number_pow(number_from_int(3), number_from_ratio(1, 3)), 30, "1.442249570307408382321638310780", 0);

        // pi + sqrt(2) etc. now evaluate instead of erroring (the general
        // fallback's whole point). Decimal digits and is_rational both
        // confirm this isn't a symbolic form.
        Num_t pi = number_pi();
        Num_t s2 = number_sqrt(number_from_int(2));
        Num_t sum = number_add(pi, s2);
        CHECK(!number_is_rational(sum));
        expect_str(sum, 10, "4.5558062160", 0);

        // sqrt of an already-irrational value (pi itself): also the
        // general fallback, not a closed form.
        Num_t sqrt_pi = number_sqrt(pi); // borrows pi
        CHECK(!number_is_rational(sqrt_pi));
        expect_str(sqrt_pi, 30, "1.772453850905516027298167483341", 0);

        // Domain errors: ln/log10 of a non-positive value, asin/acos
        // outside [-1,1], pow's 0^negative and negative-base/non-integer-
        // exponent cases, and a pole (tan(pi/2), via cos(pi/2) refining to
        // "indistinguishable from zero" at the precision cap).
        expect_error(number_ln(number_from_int(0)));
        expect_error(number_ln(number_from_int(-5)));
        expect_error(number_log10(number_from_int(-1)));
        expect_error(number_asin(number_from_int(2)));
        expect_error(number_acos(number_from_ratio(-3, 2)));
        expect_error(number_pow(number_from_int(0), number_from_int(-1)));
        expect_error(number_pow(number_from_int(-2), number_from_ratio(1, 2)));
        {
            Num_t pi2 = number_pi(), two = number_from_int(2);
            Num_t half_pi = number_div(pi2, two);
            expect_error(number_tan(half_pi)); // borrows, doesn't consume
        }
        // asin/acos are built directly on number_sqrt with no separate
        // rational-only guard, so now that number_sqrt handles irrational
        // input, an already-irrational argument works too instead of
        // erroring: asin(sin(1/2)) == 1/2 (not recognized symbolically, so
        // is_rational is false, but numerically exact to any precision).
        {
            Num_t s05 = number_sin(number_from_ratio(1, 2));
            Num_t back = number_asin(s05); // borrows, doesn't consume
            CHECK(!number_is_rational(back));
            expect_str(back, 20, "0.50000000000000000000", 0);
        }

        // Known, documented gap: identities the general engine doesn't
        // recognize symbolically refine to the precision cap and report
        // "can't decide" rather than proving true. sin(x)^2+cos(x)^2 == 1
        // is mathematically exact but not caught by any identity here, so
        // it should NOT compare equal to the rational 1. If this ever
        // starts passing, it means a real simplification was added and
        // this test (and its comment) should be revisited, not silently
        // deleted.
        {
            Num_t x = number_from_int(2);
            Num_t sinx = number_sin((x));
            Num_t cosx = number_cos(x);
            Num_t sin2 = number_mul(sinx, sinx);
            Num_t cos2 = number_mul(cosx, cosx);
            Num_t sum2 = number_add(sin2, cos2);
            CHECK(!number_equal(sum2, number_from_int(1)));
        }
    }

    // --- Symbolic form ---
    {
        expect_sym(number_from_int(42), "42");
        expect_sym(number_from_int(-17), "-17");
        expect_sym(number_from_ratio(1, 3), "1/3");
        expect_sym(number_from_ratio(-7, 2), "-7/2");
        expect_sym(NUMBER_ERROR, "(error)");
        expect_sym(factorial(25), "15511210043330985984000000");

        Num_t pi = number_pi();
        expect_sym((pi), "pi");
        expect_sym(number_neg(pi), "-pi");
        Num_t two = number_from_int(2);
        expect_sym(number_mul(two, pi), "2*pi");
        Num_t one = number_from_int(1);
        expect_sym(number_add(one, pi), "1 + pi");
        expect_sym(number_div(pi, two), "pi/2");
        Num_t twothirds = number_from_ratio(2, 3);
        expect_sym(number_mul(twothirds, pi), "2*pi/3");

        Num_t six = number_from_int(6);
        expect_sym(number_sqrt(six), "sqrt(6)"); // cf. sin(5+1) -> sin(6)
        Num_t s2 = number_sqrt(two);
        expect_sym((s2), "sqrt(2)");
        Num_t three = number_from_int(3);
        expect_sym(number_sub(three, s2), "3 - sqrt(2)");
        expect_sym(number_from_string("0.5"), "1/2"); // fraction, not decimal

        // The golden ratio: (1 + sqrt(5)) / 2
        Num_t five = number_from_int(5);
        Num_t s5 = number_sqrt(five);
        Num_t top = number_add(one, s5);
        expect_sym(number_div(top, two), "1/2 + sqrt(5)/2");
    }

    // --- TeX form ---
    {
        expect_tex(number_from_int(42), "42");
        expect_tex(number_from_int(-17), "-17");
        expect_tex(number_from_ratio(1, 3), "\\frac{1}{3}");
        expect_tex(number_from_ratio(-7, 2), "-\\frac{7}{2}");
        expect_tex(NUMBER_ERROR, "\\text{error}");
        expect_tex(factorial(25), "15511210043330985984000000");

        Num_t pi = number_pi();
        expect_tex((pi), "\\pi");
        expect_tex(number_neg(pi), "-\\pi");
        Num_t two = number_from_int(2);
        expect_tex(number_mul(two, pi), "2\\pi");
        Num_t one = number_from_int(1);
        expect_tex(number_add(one, pi), "1 + \\pi");
        expect_tex(number_div(pi, two), "\\frac{\\pi}{2}");
        Num_t twothirds = number_from_ratio(2, 3);
        expect_tex(number_mul(twothirds, pi), "\\frac{2\\pi}{3}");

        Num_t six = number_from_int(6);
        expect_tex(number_sqrt(six), "\\sqrt{6}");
        Num_t s2 = number_sqrt(two);
        Num_t three = number_from_int(3);
        expect_tex(number_sub(three, s2), "3 - \\sqrt{2}");

        // The golden ratio again: (1 + sqrt(5)) / 2
        Num_t five = number_from_int(5);
        Num_t s5 = number_sqrt(five);
        Num_t top = number_add(one, s5);
        expect_tex(number_div(top, two), "\\frac{1}{2} + \\frac{\\sqrt{5}}{2}");

        // General IRRATIONAL nodes: expression trees in TeX form.
        expect_tex(number_sin(two), "\\sin(2)");
        expect_tex(number_exp(two), "e^{2}");
        expect_tex(number_ln(two), "\\ln(2)");
        expect_tex(number_log10(two), "\\frac{\\ln(2)}{\\ln(10)}");
        Num_t s2b = number_sqrt(two);
        expect_tex(number_add(pi, s2b), "\\pi + \\sqrt{2}");
    }

    // --- Power collapsing: x*x*x displays as x^3 ---
    {
        Num_t pi = number_pi();
        Num_t two = number_from_int(2);

        Num_t pi2 = number_mul(pi, pi); // bitwise-identical operands (pi is a singleton)
        expect_sym((pi2), "pi^2");
        expect_tex((pi2), "\\pi^{2}");
        expect_sym(number_mul(pi2, pi), "pi^3");
        // int_pow's square-and-multiply tree ((pi * (pi*pi)^2)) flattens too
        Num_t five = number_from_int(5);
        expect_sym(number_pow(pi, five), "pi^5");
        expect_tex(number_pow(pi, five), "\\pi^{5}");
        // 100 = 1100100 in binary: int_pow's first accumulator step is
        // 1 * pi^4, which must not leave a "1 *" factor in the tree
        Num_t hundred = number_from_int(100);
        expect_sym(number_pow(pi, hundred), "pi^100");

        // Separately-built nodes: distinct pointers, same shape
        Num_t sin_a = number_sin(two);
        Num_t sin_b = number_sin(two);
        expect_sym(number_mul(sin_a, sin_b), "sin(2)^2");
        expect_tex(number_mul(sin_a, sin_b), "\\sin(2)^{2}");

        // Distinct factors keep the infix chain around the collapsed power
        expect_sym(number_mul(pi2, sin_a), "pi^2*sin(2)");
        expect_tex(number_mul(pi2, sin_a), "\\pi^{2} \\cdot \\sin(2)");

        // Bases that bind looser than '^' get parenthesized: a real-kind
        // form in both representations, and e^{2} only in TeX (where a bare
        // double superscript would be invalid)
        Num_t one = number_from_int(1);
        Num_t onepi = number_add(one, pi);
        expect_sym(number_mul(onepi, onepi), "(1 + pi)^2");
        expect_tex(number_mul(onepi, onepi), "(1 + \\pi)^{2}");
        // exp(pi), not exp(2): a rational argument now unifies exp(x)*exp(x)
        // into the closed EXP(2x) form (see real_mul), so it wouldn't reach
        // this IRR_MUL/IRR_EXP parenthesization path at all, so pi keeps this
        // exercising the general-fallback case.
        Num_t exp_a = number_exp(pi);
        Num_t exp_b = number_exp(pi);
        expect_sym(number_mul(exp_a, exp_b), "exp(pi)^2");
        expect_tex(number_mul(exp_a, exp_b), "(e^{\\pi})^{2}");

        // tan(2) is a DIV node: already parenthesized symbolically, but
        // \frac needs wrapping before an exponent
        Num_t tan_a = number_tan(two);
        Num_t tan_b = number_tan(two);
        expect_sym(number_mul(tan_a, tan_b), "(sin(2)/cos(2))^2");
        expect_tex(number_mul(tan_a, tan_b), "(\\frac{\\sin(2)}{\\cos(2)})^{2}");

        // Factors that aren't provably identical don't collapse
        Num_t three = number_from_int(3);
        Num_t sin2 = number_sin(two);
        Num_t sin3 = number_sin(three);
        expect_sym(number_mul(sin2, sin3), "sin(2)*sin(3)");
    }

    // --- Infix operands that render looser than their context get parens ---
    {
        Num_t pi = number_pi();
        Num_t one = number_from_int(1);
        Num_t two = number_from_int(2);
        Num_t onepi = number_add(one, pi); // "1 + pi"
        Num_t s2 = number_sqrt2();
        Num_t onesqrt2 = number_add(one, s2); // "1 + sqrt(2)"
        Num_t twopi = number_mul(two, pi); // "2*pi"
        Num_t sin2 = number_sin(two);

        // The reported bug: (1+pi)*(1+sqrt(2)) printed as "(1 + pi * 1 + sqrt(2))"
        expect_sym(number_mul(onepi, onesqrt2), "(1 + pi)*(1 + sqrt(2))");
        expect_tex(number_mul(onepi, onesqrt2), "(1 + \\pi) \\cdot (1 + \\sqrt{2})");
        expect_sym(number_mul(onepi, sin2), "(1 + pi)*sin(2)");

        // Division: a sum on either side, and even a product on the right
        expect_sym(number_div(sin2, onepi), "sin(2)/(1 + pi)");
        expect_sym(number_div(onepi, sin2), "(1 + pi)/sin(2)");
        expect_sym(number_div(sin2, twopi), "sin(2)/(2*pi)");
        expect_sym(number_div(twopi, sin2), "2*pi/sin(2)"); // left product binds fine

        // Subtraction/addition: sin(2) - (1+pi) becomes ADD of the negated
        // form, whose "-1 - pi" rendering leads with a sign
        expect_sym(number_sub(sin2, onepi), "sin(2) + (-1 - pi)");
        expect_sym(number_add(sin2, onepi), "sin(2) + (1 + pi)");
        expect_tex(number_add(sin2, onepi), "\\sin(2) + (1 + \\pi)");

        // IRRATIONAL infix nodes no longer self-parenthesize, so contexts
        // must wrap them like any other loose-binding operand
        Num_t pisum = number_add(pi, s2); // "pi + sqrt(2)": a SUM node
        expect_sym((pisum), "pi + sqrt(2)");
        expect_sym(number_mul(pisum, sin2), "(pi + sqrt(2))*sin(2)");
        expect_sym(number_div(sin2, pisum), "sin(2)/(pi + sqrt(2))");
        Num_t pi2 = number_mul(pi, pi);
        expect_sym(number_div(sin2, pi2), "sin(2)/pi^2"); // a lone power binds like an atom
        Num_t pisin = number_mul(pi, sin2);
        expect_sym(number_div(sin2, pisin), "sin(2)/(pi*sin(2))");
    }

    // --- Reference counting ---
    {
        Num_t x = factorial(25);
        Num_t y = (x);
        expect_str(y, 0, "15511210043330985984000000", 1); // still alive via y
    }

    // --- Rounding: floor/ceil/trunc/round across all tiers ---
    {
        // Small rationals, both signs, against the mathematical definitions.
        struct {
            int64_t n, d, floor, ceil, trunc, round;
        } cases[] = {
            {0, 1, 0, 0, 0, 0},          {7, 1, 7, 7, 7, 7},    {-7, 1, -7, -7, -7, -7},
            {7, 2, 3, 4, 3, 4}, // 3.5: tie -> 4 (even)
            {-7, 2, -4, -3, -3, -4}, // -3.5: tie -> -4 (even)
            {5, 2, 2, 3, 2, 2}, // 2.5: tie -> 2 (even)
            {-5, 2, -3, -2, -2, -2},     {1, 3, 0, 1, 0, 0},    {-1, 3, -1, 0, 0, 0},
            {2, 3, 0, 1, 0, 1},          {-2, 3, -1, 0, 0, -1}, {99, 10, 9, 10, 9, 10},
            {-99, 10, -10, -9, -9, -10},
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
            Num_t x = number_from_ratio(cases[i].n, cases[i].d);
            expect_eq(number_floor(x), number_from_int(cases[i].floor));
            expect_eq(number_ceil(x), number_from_int(cases[i].ceil));
            expect_eq(number_trunc(x), number_from_int(cases[i].trunc));
            expect_eq(number_round(x), number_from_int(cases[i].round));
        }

        // Big rationals: 25!/29 has a huge inexact quotient (29 is prime
        // and > 25, so it divides nothing in 25!); floor/ceil bracket it
        // and differ by exactly 1. 25! itself (an integer bigrat) rounds
        // to itself under every mode.
        Num_t f = factorial(25);
        Num_t twentynine = number_from_int(29);
        Num_t x = number_div(f, twentynine);
        Num_t fl = number_floor(x), ce = number_ceil(x);
        expect_eq(number_sub(ce, fl), number_from_int(1)); // x isn't an integer
        expect_str((fl), 0, "534869311838999516689655", 1);
        expect_eq(number_floor(f), (f));
        expect_eq(number_round(f), (f));
        Num_t nf = number_neg(f);
        expect_eq(number_trunc(nf), (nf));

        // Symbolic irrationals: decided by refinement, exactly.
        Num_t pi = number_pi();
        expect_eq(number_floor(pi), number_from_int(3));
        expect_eq(number_ceil(pi), number_from_int(4));
        expect_eq(number_round(pi), number_from_int(3));
        Num_t npi = number_neg(pi);
        expect_eq(number_floor(npi), number_from_int(-4));
        expect_eq(number_ceil(npi), number_from_int(-3));
        expect_eq(number_trunc(npi), number_from_int(-3));
        expect_eq(number_round(npi), number_from_int(-3));
        Num_t s2 = number_sqrt2();
        expect_eq(number_floor(s2), number_from_int(1));
        expect_eq(number_round(s2), number_from_int(1));

        // General IRRATIONAL nodes, including trunc straddling zero: sin(1)
        // is in (0, 1) and -sin(1) in (-1, 0), so all four roundings are
        // decided by refinement (and trunc of both is 0).
        Num_t one = number_from_int(1);
        Num_t s = number_sin(one);
        expect_eq(number_floor(s), number_from_int(0));
        expect_eq(number_ceil(s), number_from_int(1));
        expect_eq(number_trunc(s), number_from_int(0));
        expect_eq(number_round(s), number_from_int(1)); // sin(1) = 0.841...
        Num_t ns = number_neg(s);
        expect_eq(number_trunc(ns), number_from_int(0));
        expect_eq(number_round(ns), number_from_int(-1));
        Num_t e = number_exp(one);
        expect_eq(number_round(e), number_from_int(3)); // e = 2.718...

        // Errors propagate.
        expect_error(number_floor(NUMBER_ERROR));
        expect_error(number_round(NUMBER_ERROR));
    }

    // --- number_mod ---
    {
        // Floored modulus: result takes b's sign, and a == b*floor(a/b) +
        // mod(a, b) exactly.
        struct {
            int64_t an, ad, bn, bd, rn, rd;
        } cases[] = {
            {7, 1, 3, 1, 1, 1},    {-7, 1, 3, 1, 2, 1}, {7, 1, -3, 1, -2, 1},
            {-7, 1, -3, 1, -1, 1}, {6, 1, 3, 1, 0, 1},  {7, 2, 2, 1, 3, 2}, // 3.5 mod 2 = 1.5
            {1, 2, 1, 3, 1, 6}, // 1/2 mod 1/3 = 1/6
            {-1, 2, 1, 3, 1, 6}, // -1/2 mod 1/3 = 1/6 (floored, not truncated)
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
            Num_t a = number_from_ratio(cases[i].an, cases[i].ad);
            Num_t b = number_from_ratio(cases[i].bn, cases[i].bd);
            Num_t m = number_mod(a, b);
            expect_eq((m), number_from_ratio(cases[i].rn, cases[i].rd));
            // The defining identity, reconstructed exactly.
            Num_t q = number_div(a, b);
            Num_t qi = number_floor(q);
            Num_t bq = number_mul(b, qi);
            expect_eq(number_add(bq, m), (a));
        }

        // Range reduction against an irrational modulus stays symbolic and
        // exact: 10 mod pi = 10 - 3*pi.
        Num_t ten = number_from_int(10);
        Num_t pi = number_pi();
        Num_t m = number_mod(ten, pi);
        Num_t three = number_from_int(3);
        Num_t threepi = number_mul(three, pi);
        expect_eq((m), number_sub(ten, threepi));

        // b == 0 and error operands.
        Num_t zero = number_from_int(0);
        expect_error(number_mod(ten, zero));
        expect_error(number_mod(NUMBER_ERROR, ten));
        expect_error(number_mod(ten, NUMBER_ERROR));
    }

    // --- Correctly rounded number_to_double for general (DAG-backed)
    // irrationals, against independently-computed nearest doubles. These
    // conversions are served by the hardware double-interval fast path when
    // it's enabled (the default) and by exact bigint bracketing when it
    // isn't (-DNUMBER_NO_DOUBLE_FILTER). The expected values are the
    // same either way, so this cross-checks the two implementations
    // against each other in whichever build is running. References were
    // computed by rounding 75-digit exact decimal expansions to the
    // nearest double, independently of libm (whose sin/exp/ln are not
    // guaranteed correctly rounded and must NOT be used as references
    // here). ---
    {
        Num_t one = number_from_int(1);
        Num_t e1 = number_exp(one);
        CHECK(number_to_double(e1) == 0x1.5bf0a8b145769p+1); // e
        Num_t two = number_from_int(2);
        Num_t l2 = number_ln(two);
        CHECK(number_to_double(l2) == 0x1.62e42fefa39efp-1); // ln 2
        Num_t s1 = number_sin(one);
        CHECK(number_to_double(s1) == 0x1.aed548f090ceep-1); // sin 1
        Num_t a2 = number_atan(two);
        CHECK(number_to_double(a2) == 0x1.1b6e192ebbe44p+0); // atan 2
        Num_t pi = number_pi();
        Num_t epi = number_exp(pi);
        CHECK(number_to_double(epi) == 0x1.724046eb0933ap+4); // e^pi

        // Comparisons decided through the sign filter agree with the exact
        // ordering: e < pi, and e against tight rational brackets either
        // side (e = 2.71828182845904523536...).
        Num_t e2 = number_exp(one);
        CHECK(number_compare(e2, pi) < 0);
        CHECK(number_compare(pi, e2) > 0);
        Num_t lo_ref = number_from_ratio(2718281828, 1000000000);
        Num_t hi_ref = number_from_ratio(2718281829, 1000000000);
        CHECK(number_compare(e2, lo_ref) > 0);
        CHECK(number_compare(e2, hi_ref) < 0);
    }

    // --- number_is_integer / number_to_int64 ---
    {
        bool ok;
        Num_t five = number_from_int(5);
        CHECK(number_is_integer(five));
        CHECK(number_to_int64(five, &ok) == 5 && ok);

        Num_t half = number_from_ratio(1, 2);
        CHECK(!number_is_integer(half));
        CHECK(number_to_int64(half, &ok) == 0 && !ok);

        Num_t zero = number_from_int(0);
        CHECK(number_is_integer(zero));
        CHECK(number_to_int64(zero, &ok) == 0 && ok); // 0 with ok true really is zero

        // int64 boundaries: INT64_MAX and INT64_MIN are bigrats (out of
        // tier 1) that fit exactly; one past either end doesn't.
        Num_t imax = number_from_int(INT64_MAX);
        CHECK(number_is_integer(imax));
        CHECK(number_to_int64(imax, &ok) == INT64_MAX && ok);
        Num_t imin = number_from_int(INT64_MIN);
        CHECK(number_to_int64(imin, &ok) == INT64_MIN && ok);
        Num_t one = number_from_int(1);
        Num_t over = number_add(imax, one);
        CHECK(number_is_integer(over)); // still an integer, just not an int64
        CHECK(number_to_int64(over, &ok) == 0 && !ok);
        Num_t under = number_sub(imin, one);
        CHECK(number_to_int64(under, &ok) == 0 && !ok);

        Num_t big = factorial(25);
        CHECK(number_is_integer(big));
        CHECK(number_to_int64(big, &ok) == 0 && !ok);

        // Irrationals: never provably integer, never convertible.
        Num_t pi = number_pi();
        CHECK(!number_is_integer(pi));
        CHECK(number_to_int64(pi, &ok) == 0 && !ok);
        Num_t s2 = number_sqrt2();
        CHECK(!number_is_integer(s2));

        CHECK(!number_is_integer(NUMBER_ERROR));
        CHECK(number_to_int64(NUMBER_ERROR, &ok) == 0 && !ok);
    }

    // --- log2: exact on powers of two, correctly rounded otherwise ---
    {
        expect_str(number_log2(number_from_int(8)), 5, "3", 1);
        expect_str(number_log2(number_from_int(1024)), 5, "10", 1);
        expect_str(number_log2(number_from_int(2)), 5, "1", 1);
        expect_str(number_log2(number_from_int(1)), 5, "0", 1);
        expect_str(number_log2(number_from_ratio(1, 4)), 5, "-2", 1);
        expect_str(number_log2(number_from_int(3)), 6, "1.584963", 0);
        CHECK(number_is_error(number_log2(number_from_int(0))));
        CHECK(number_is_error(number_log2(number_from_int(-1))));
    }

    // --- atan2: quadrants, axes, and the undefined origin ---
    {
        Num_t pi = number_pi();
        Num_t halfpi = number_mul(pi, NUMBER_SMALL(1, 2));

        Num_t up = number_atan2(number_from_int(1), number_from_int(0)); // +pi/2
        CHECK(number_equal(up, halfpi));

        Num_t down = number_atan2(number_from_int(-1), number_from_int(0)); // -pi/2
        Num_t neghalfpi = number_neg(halfpi);
        CHECK(number_equal(down, neghalfpi));

        Num_t left = number_atan2(number_from_int(0), number_from_int(-1)); // pi
        CHECK(number_equal(left, pi));

        Num_t origin_r = number_atan2(number_from_int(0), number_from_int(1)); // 0
        CHECK(number_is_zero(origin_r));

        Num_t pi4 = number_mul(pi, NUMBER_SMALL(1, 4));
        Num_t a11 = number_atan2(number_from_int(1), number_from_int(1)); // pi/4
        CHECK(number_equal(a11, pi4));

        Num_t pi34 = number_mul(pi, NUMBER_SMALL(3, 4));
        Num_t a1m1 = number_atan2(number_from_int(1), number_from_int(-1)); // 3*pi/4
        CHECK(number_equal(a1m1, pi34));

        CHECK(number_is_error(number_atan2(number_from_int(0), number_from_int(0))));
        expect_str(number_atan2(number_from_int(1), number_from_int(2)), 6, "0.463648", 0);
    }

    // --- gcd/lcm: integers, sign normalization, zeros, rationals, bigints ---
    {
        expect_str(number_gcd(number_from_int(12), number_from_int(18)), 0, "6", 1);
        expect_str(number_lcm(number_from_int(12), number_from_int(18)), 0, "36", 1);
        expect_str(number_gcd(number_from_int(-12), number_from_int(18)), 0, "6", 1);
        expect_str(number_lcm(number_from_int(-4), number_from_int(6)), 0, "12", 1);
        expect_str(number_gcd(number_from_int(0), number_from_int(5)), 0, "5", 1);
        expect_str(number_gcd(number_from_int(5), number_from_int(0)), 0, "5", 1);
        expect_str(number_gcd(number_from_int(0), number_from_int(0)), 0, "0", 1);
        expect_str(number_lcm(number_from_int(0), number_from_int(5)), 0, "0", 1);
        expect_sym(number_gcd(number_from_ratio(1, 2), number_from_ratio(1, 3)), "1/6");
        expect_str(number_lcm(number_from_ratio(1, 2), number_from_ratio(1, 3)), 0, "1", 1);

        Num_t p100 = number_pow(number_from_int(2), number_from_int(100));
        Num_t p60 = number_pow(number_from_int(2), number_from_int(60));
        Num_t g = number_gcd(p100, p60); // 2^60
        CHECK(number_equal(g, p60));
        Num_t l = number_lcm(p100, p60); // 2^100
        CHECK(number_equal(l, p100));

        Num_t s2 = number_sqrt2(), two = number_from_int(2);
        CHECK(number_is_error(number_gcd(s2, two))); // irrational operand -> error
        CHECK(number_is_error(number_lcm(two, s2)));
    }

    // --- min/max: exact where decidable, `digits` fallback for ties ---
    {
        expect_str(number_min(number_from_int(3), number_from_int(5), 10), 0, "3", 1);
        expect_str(number_max(number_from_int(3), number_from_int(5), 10), 0, "5", 1);
        // 1/3 == 0.3333... is larger than 0.333 exactly; the exact compare
        // decides it (digits is not consulted).
        expect_sym(number_min(number_from_ratio(1, 3), number_from_ratio(333, 1000), 2), "333/1000");
        expect_sym(number_max(number_from_ratio(1, 3), number_from_ratio(333, 1000), 2), "1/3");
        // A tie returns the first operand.
        expect_str(number_min(number_from_int(5), number_from_int(5), 4), 0, "5", 1);

        Num_t pi = number_pi(), three = number_from_int(3);
        Num_t mn = number_min(pi, three, 10); // pi > 3
        CHECK(number_equal(mn, three));
        Num_t mx = number_max(pi, three, 10);
        CHECK(number_equal(mx, pi));

        Num_t s2 = number_sqrt2(), f14 = number_from_ratio(7, 5); // sqrt(2) > 1.4
        Num_t mx2 = number_max(s2, f14, 10);
        CHECK(number_equal(mx2, s2));

        CHECK(number_is_error(number_min(NUMBER_ERROR, number_from_int(5), 3)));
        CHECK(number_is_error(number_max(number_from_int(5), NUMBER_ERROR, 3)));
    }

    printf("all %d checks passed\n", checks);
    return 0;
}
