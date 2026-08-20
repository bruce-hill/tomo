// number.c — the number datatype: small rational immediates, heap-allocated
// big rationals over a GMP-backed bigint, and the tier-3 constructive-real
// approximation engine for irrational values. See number-design.md.

#include "number.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The numeric substrate is GMP: a heap big rational is a canonical mpq_t,
// tier 3's magnitudes are heap mpz_t's behind the thin bi_* ownership
// veneer below, and the hot approximation loops run on in-place mpz_t
// locals. See number-design.md's "Bigint backend" note.
#include <gmp.h>

#include <gc.h>

// ---------------------------------------------------------------------------
// Allocation
//
// Every heap allocation funnels through xmalloc, which is the Boehm
// collector, and nothing is ever explicitly released.
//
// GC_MALLOC (not GC_MALLOC_ATOMIC) is required here: tier-3 nodes hold
// `number` fields that are themselves pointers to other heap objects, so the
// collector must scan them to keep a DAG's interior nodes alive. GMP's limb
// buffers go through GMP's own process-global allocator rather than this
// one; tomo_init() (stdlib.c) already points that at GC_MALLOC_ATOMIC, which
// is both correct (limbs hold no pointers) and better than scanning them.

static void *xmalloc(size_t size)
{
    void *p = GC_MALLOC(size);
    if (!p) abort();
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static char *xsprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) abort();
    char *s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

// ---------------------------------------------------------------------------
// Instrumentation (opt-in via -DNUMBER_STATS; dump via number_stats_dump,
// declared in number.h under the same guard). Dispatch counts only -- no
// timing here, since wall-clock belongs to a profiler (perf, callgrind,
// rdtsc microbenchmarks, ...), not the library itself. This answers a
// different question than a profiler: not "how slow is this call" but
// "which tier and which path do real workloads' calls actually take,"
// which is what should decide where profiling effort goes next.
//
// Plain (non-atomic) counters: this code isn't thread-safe elsewhere
// either, so a stats build doesn't add synchronization no other part of it
// needs.

#ifdef NUMBER_STATS
enum { STAT_ADD, STAT_SUB, STAT_MUL, STAT_DIV, STAT_COMPARE, STAT_OP_COUNT };

static struct {
    uint64_t ops[STAT_OP_COUNT]; // top-level number_* calls, by operation
    uint64_t tier1_fastpath;     // small x small, integer sub-path (both denominators 1)
    uint64_t tier1_general;      // small x small, needs gcd reduction / cross-multiply
    uint64_t tier2;              // at least one heap bigrat operand
    uint64_t tier2_u128_fast;    // tier2 op whose operands both still fit u64 -- no bigint temporaries
    uint64_t tier3;              // at least one real/irrational operand
    uint64_t promotions;         // number_from_ratio64 result didn't fit back into tier 1
    uint64_t bi_allocs;          // heap-bigint (mpz) allocations -- tier-3 magnitudes route through bi_new
    uint64_t irr_memo_hits;      // irr_fixed: memoized approximation already precise enough
    uint64_t irr_recompute[10];  // irr_fixed: recompute needed, indexed by IRR_* (10 ops; see enum below)
    uint64_t interval_hits;      // double-interval fast path decided a sign/rounding outright
    uint64_t interval_fallbacks; // interval was computable but too wide (or undecidable): bigint path
} number_stats;

#define NSTAT(field) (number_stats.field++)
#else
#define NSTAT(field) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Tags and small-rational packing

#define TAG_MASK 0x3u
#define TAG_POINTER 0x0u
#define TAG_SMALL 0x1u
#define TAG_ERROR 0x3u

#define SMALL_NUM_MAX INT32_MAX  // |numerator| bound for the immediate form
#define SMALL_DEN_MAX 0x3FFFFFFFu // 30-bit denominator bound

static inline uint32_t number_tag(number x) { return x.bits & TAG_MASK; }

static inline number small_make(int32_t num, uint32_t den)
{
    return (number){((uint64_t)(uint32_t)num << 32) | ((uint64_t)den << 2) | TAG_SMALL};
}

static inline int64_t small_num(number x) { return (int32_t)(x.bits >> 32); }
static inline uint64_t small_den(number x) { return (x.bits >> 2) & SMALL_DEN_MAX; }

static inline number small_zero(void) { return small_make(0, 1); }

// ---------------------------------------------------------------------------
// Error values
//
// The error tag (0x3) only pins the low 2 bits (see TAG_MASK/TAG_ERROR
// above) -- number_is_error checks number_tag(x) == TAG_ERROR, not bit-exact
// equality to NUMBER_ERROR -- so the remaining 62 bits are free to carry a
// reason code with no change to the immediate representation: an error
// value is still tagless -- no heap object at all, whatever its payload
// bits. NUMBER_ERROR itself
// (0x3) is ERR_GENERIC (code 0, shifted in), so every existing bare
// NUMBER_ERROR return continues to work unchanged, just with a generic
// message; call sites that know *why* construct a specific code instead via
// err().
enum {
    ERR_GENERIC = 0,     // NUMBER_ERROR's own code: no specific reason given
    ERR_DIV_BY_ZERO,
    ERR_SQRT_NEGATIVE,
    ERR_LOG_NONPOSITIVE,
    ERR_ASIN_ACOS_DOMAIN,
    ERR_POW_ZERO_NEGATIVE,
    ERR_POW_NEGATIVE_BASE,
    ERR_NOT_FINITE,
    ERR_PARSE,
    ERR_UNDECIDABLE,
    ERR_UNDECIDABLE_INT,
    ERR_ATAN2_ORIGIN,
    ERR_GCD_IRRATIONAL,
    ERR_COUNT,
};

static const char *const ERROR_MESSAGES[ERR_COUNT] = {
    [ERR_GENERIC] = "undefined result",
    [ERR_DIV_BY_ZERO] = "division by zero",
    [ERR_SQRT_NEGATIVE] = "square root of a negative number",
    [ERR_LOG_NONPOSITIVE] = "logarithm of a non-positive number",
    [ERR_ASIN_ACOS_DOMAIN] = "arcsine or arccosine of a value outside [-1, 1]",
    [ERR_POW_ZERO_NEGATIVE] = "zero raised to a negative power",
    [ERR_POW_NEGATIVE_BASE] = "a negative number raised to a non-integer power (not a real result)",
    [ERR_NOT_FINITE] = "not a finite number (NaN or infinity)",
    [ERR_PARSE] = "invalid number syntax",
    [ERR_UNDECIDABLE] = "an irrational value's sign could not be resolved within the precision limit",
    [ERR_UNDECIDABLE_INT] = "an irrational value's integer part could not be resolved within the precision limit",
    [ERR_ATAN2_ORIGIN] = "atan2 of (0, 0) (the angle is undefined)",
    [ERR_GCD_IRRATIONAL] = "greatest common divisor or least common multiple of an irrational value",
};

// An error value carrying a specific reason code. err(ERR_GENERIC) is bit-
// identical to NUMBER_ERROR.
static inline number err(uint32_t code) { return (number){((uint64_t)code << 2) | TAG_ERROR}; }

// ---------------------------------------------------------------------------
// Bigint: a heap-allocated GMP integer (mpz_t), used magnitude-only -- every
// value stored here is nonnegative; signs live in the containing object
// (num_bigrat's sign field, num_irr's appr_sign). The struct itself comes
// from xmalloc (the collector); the limb buffer inside is owned by GMP and
// follows GMP's process-global allocator, which tomo_init() (stdlib.c)
// already points at the collector.

// unsigned __int128 is a GCC/Clang extension, not ISO C -- __extension__
// suppresses the -Wpedantic warning for introducing it (verified clean
// under this project's exact warning set); every other u128 use below is
// just referencing this typedef, not introducing new non-standard syntax,
// so it needs no further __extension__ markers.
__extension__ typedef unsigned __int128 u128;

typedef mp_limb_t bi_limb;

typedef __mpz_struct bigint;

static bigint *bi_new(void)
{
    NSTAT(bi_allocs);
    bigint *b = xmalloc(sizeof(bigint));
    mpz_init(b);
    return b;
}

// NULL-safe, like free(): the memoized appr fields stay NULL until first use.
static void bi_free(bigint *b)
{
    if (!b) return;
    mpz_clear(b);
}

static bigint *bi_from_u64(uint64_t v)
{
    bigint *b = bi_new();
    mpz_set_ui(b, v);
    return b;
}

static bigint *bi_copy(const bigint *a)
{
    bigint *b = bi_new();
    mpz_set(b, a);
    return b;
}

static inline bool bi_is_zero(const bigint *a) { return mpz_sgn(a) == 0; }

static inline bool bi_is_one(const bigint *a) { return mpz_cmp_ui(a, 1) == 0; }

static bool bi_fits_u64(const bigint *a, uint64_t *out)
{
    if (mpz_size(a) > 1) return false;
    *out = mpz_size(a) ? mpz_getlimbn(a, 0) : 0;
    return true;
}

// Write u128 v into an already-initialized mpz (magnitude only). The hi==0
// case -- the common one, since a u128 here is usually a widened u64 -- is a
// single mpz_set_ui with no shift.
static void mpz_set_u128(mpz_ptr dst, u128 v)
{
    uint64_t hi = (uint64_t)(v >> 64);
    if (hi == 0) {
        mpz_set_ui(dst, (uint64_t)v);
        return;
    }
    mpz_set_ui(dst, hi);
    mpz_mul_2exp(dst, dst, 64);
    mpz_add_ui(dst, dst, (uint64_t)v);
}

// Binary (Stein's) GCD: shifts/subtracts/ctz instead of Euclid's
// modulo loop. 64-bit idiv is the slowest ALU op on x86 (the arith_bench
// int64-vs-double div numbers show it directly), and Euclid's loop is
// nothing but idivs -- measured on this machine, binary gcd is ~1.7x
// faster on small-rational-sized operands, which cuts the dominant cost
// of every small-fraction operation (see number_from_ratio64 and the
// coprime-denominator reduction in rat_addsub_u128). Same contract as the
// Euclid version it replaced, including gcd(x,0) == x and gcd(0,x) == x.
static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    if (a == 0) return b;
    if (b == 0) return a;
    // gcd(x, 1) == 1 in one step -- the hot case for integer operands, where
    // rat_mul_u128/rat_div_u128 take gcd(n, 1) against a denominator of 1 (and
    // rat_addsub_u128 takes gcd(da, db) with both 1). Without this, binary GCD
    // would grind ~log(x) subtract-shift iterations down to 1. Mirrors the
    // same guard in gcd_u128.
    if (a == 1 || b == 1) return 1;
    int shift = __builtin_ctzll(a | b); // common factors of 2
    a >>= __builtin_ctzll(a);
    for (;;) {
        b >>= __builtin_ctzll(b); // b nonzero here: loop exits before b hits 0
        if (a > b) {
            uint64_t t = a;
            a = b;
            b = t;
        }
        b -= a; // both odd, so b-a is even: next ctz strips >= 1 bit
        if (b == 0) break;
    }
    return a << shift;
}

// Count trailing zeros of a nonzero u128 (there's no __builtin_ctz for
// 128-bit): low word if it carries any set bit, else 64 + the high word's.
static int ctz_u128(u128 v)
{
    uint64_t lo = (uint64_t)v;
    return lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll((uint64_t)(v >> 64));
}

// Binary (Stein's) GCD, the u128 twin of gcd_u64 above -- shifts/subtracts/
// ctz instead of Euclid's modulo loop. This matters more here than at 64
// bits: u128 has no hardware divide at all (unlike gcd_u64, whose modulo was
// at least one idiv), so the old `a % b` loop compiled to a full software
// division routine per iteration. The one caller left is rat_addsub_u128's
// g>1 leftover reduction gcd(num, g); add/sub with coprime denominators and
// all of mul/div reduce with 64-bit gcds only, never reaching here. Same
// contract as before, including gcd(x,0) == x and gcd(0,x) == x.
static u128 gcd_u128(u128 a, u128 b)
{
    if (a == 0) return b;
    if (b == 0) return a;
    // gcd(x, 1) == 1 in one step. This is the hot case for *integer*
    // operands, whose denominator is 1 (every large-integer add/sub/mul
    // reduces num/1): without this guard binary GCD would grind ~log(num)
    // subtract-shift iterations down to 1, where Euclid's modulo collapsed
    // x % 1 == 0 immediately. The both-large case that motivates the binary
    // algorithm (coprime fraction num/den) essentially never hits this.
    if (a == 1 || b == 1) return 1;
    int shift = ctz_u128(a | b); // common factors of 2
    a >>= ctz_u128(a);
    for (;;) {
        b >>= ctz_u128(b); // b nonzero here: loop exits before b hits 0
        if (a > b) {
            u128 t = a;
            a = b;
            b = t;
        }
        b -= a; // both odd, so b-a is even: next ctz strips >= 1 bit
        if (b == 0) break;
    }
    return a << shift;
}

static int bi_cmp(const bigint *a, const bigint *b) { return mpz_cmp(a, b); }

static uint32_t bi_bitlen(const bigint *a)
{
    if (mpz_sgn(a) == 0) return 0;
    return (uint32_t)mpz_sizeinbase(a, 2);
}

static bigint *bi_add(const bigint *a, const bigint *b)
{
    bigint *res = bi_new();
    mpz_add(res, a, b);
    return res;
}

// Callers maintain a >= b (magnitude convention: results stay nonnegative);
// mpz_sub itself has no such requirement.
static bigint *bi_sub(const bigint *a, const bigint *b)
{
    bigint *res = bi_new();
    mpz_sub(res, a, b);
    return res;
}

static bigint *bi_mul(const bigint *a, const bigint *b)
{
    bigint *res = bi_new();
    mpz_mul(res, a, b);
    return res;
}

static bigint *bi_mul_u32(const bigint *a, uint32_t m)
{
    bigint *res = bi_new();
    mpz_mul_ui(res, a, m);
    return res;
}

static bigint *bi_shl(const bigint *a, uint32_t k)
{
    bigint *res = bi_new();
    mpz_mul_2exp(res, a, k);
    return res;
}

static bigint *bi_shr(const bigint *a, uint32_t k)
{
    bigint *res = bi_new();
    mpz_tdiv_q_2exp(res, a, k);
    return res;
}

// d must be nonzero.
static void bi_divmod(const bigint *n, const bigint *d, bigint **q_out, bigint **r_out)
{
    bigint *q = bi_new(), *r = bi_new();
    mpz_tdiv_qr(q, r, n, d);
    *q_out = q;
    *r_out = r;
}

// floor(sqrt(n)).
static bigint *bi_isqrt(const bigint *n)
{
    bigint *s = bi_new();
    mpz_sqrt(s, n);
    return s;
}

static bigint *bi_pow10(uint32_t k)
{
    bigint *r = bi_new();
    mpz_ui_pow_ui(r, 10, k);
    return r;
}

// If n (a positive bigint magnitude) is exactly base^k for some k >= 0, sets
// *out = k and returns true. base is a GMP sizeinbase base (2..62).
static bool bi_exact_power(const bigint *n, unsigned base, int64_t *out)
{
    if (bi_is_zero(n)) return false;
    // sizeinbase is the digit count (or one more); base^k is the only
    // (k+1)-digit value in that base, so compute the candidate and compare.
    size_t k = mpz_sizeinbase(n, (int)base) - 1;
    mpz_t p;
    mpz_init(p);
    mpz_ui_pow_ui(p, base, k);
    bool eq = mpz_cmp(p, n) == 0;
    mpz_clear(p);
    if (eq) *out = (int64_t)k;
    return eq;
}

// Decimal digits of a magnitude, as an xmalloc'd string ("0" for zero).
// mpz_get_str is subquadratic (divide-and-conquer conversion), unlike the
// old chunked divide-by-10^19 loop this replaces.
static char *bi_to_decimal(const bigint *b)
{
    char *buf = xmalloc(mpz_sizeinbase(b, 10) + 2);
    mpz_get_str(buf, 10, b);
    return buf;
}

// Decimal digits of |z| for a possibly-negative mpz (an mpq numref): the
// printers place signs themselves, per their own spacing/TeX conventions.
static char *mpz_mag_decimal(mpz_srcptr z)
{
    char *buf = xmalloc(mpz_sizeinbase(z, 10) + 2);
    mpz_get_str(buf, 10, z);
    if (buf[0] == '-') memmove(buf, buf + 1, strlen(buf));
    return buf;
}

// ---------------------------------------------------------------------------
// Heap objects: big rationals (canonical: gcd(num,den)==1, den > 0, sign in
// its own field, and the value does NOT fit in a small rational).

enum { KIND_BIGRAT = 1, KIND_REAL = 2, KIND_IRRATIONAL = 3 };

// Common prefix of every heap object. kind holds one of the three KIND_*
// values above; the rest of the word is unused.
typedef struct {
    uint32_t kind : 2;
} num_head;

typedef struct {
    num_head head;
    // mpq_t is a one-element array of __mpq_struct, so this field embeds the
    // num/den __mpz_struct pair directly in the allocation (no pointer
    // indirection): a bigrat is one xmalloc plus GMP's two limb buffers.
    // Canonical: den > 0, gcd(num,den) == 1, sign in num, and the value does
    // not fit the small immediate tier.
    mpq_t q;
} num_bigrat;

// Tier 3: an irrational real, stored as the linear form a + b*F where a and b
// are rationals (b nonzero) and F = fn(arg) is a transcendental function
// applied to arg: pi (arg unused), sqrt(arg) for a positive non-square
// integer arg, ln(arg) for a rational arg > 1 (see make_real's
// canonicalization -- 0 < arg < 1 is rewritten as -ln(1/arg) so F stays
// positive, matching the invariant every other fn already relies on), or
// exp(arg) for any rational arg. fn is generic on purpose -- more
// transcendentals (sin, cos, tan, ...) can join later without renaming
// anything. Each sqrt(n)-class is a field (Q(sqrt n)) and the pi form is
// closed under affine operations, so most arithmetic stays symbolic; ln/exp
// are closed under narrower rules (see real_add/real_mul/real_div: same-arg
// affine combination for both, plus exp(a)*exp(b) = exp(a+b) and
// ln(a)+ln(b) = ln(a*b) generalized to integer linear combinations); results
// outside every form fall back to the general IRRATIONAL engine.
enum { FN_PI = 1, FN_SQRT = 2, FN_LN = 3, FN_EXP = 4 };

typedef struct {
    num_head head;
    uint32_t fn : 3;     // FN_PI/FN_SQRT/FN_LN/FN_EXP: which transcendental
    number a;             // rational part (owned)
    number b;             // coefficient of F: nonzero rational (owned)
    number arg;           // FN_SQRT: positive non-square integer; FN_LN: rational > 1;
                          // FN_EXP: any rational; unused for FN_PI
    mpz_t appr;           // memoized approximation of F: F * 2^appr_prec
    uint32_t appr_prec;   // bits of precision in appr (error <= 2 ulp); 0 = no
                          // memo yet (every stored prec carries a +64 overshoot,
                          // so 0 never occurs naturally); unbounded, full-width
} num_real;

// Tier 3 (general fallback): a lazy constructive-real DAG node, used once a
// computation falls outside every closed symbolic form above (pi + sqrt(2),
// pi*pi, sin(2), ...). Evaluated to arbitrary bit precision by bigint
// fixed-point approximation (arctan_recip/machin_pi below is the prototype
// this generalizes), memoizing the best-known approximation like num_real's
// appr/appr_prec. Most of the calculator's transcendental functions (tan,
// asin, acos, sinh, cosh, tanh, log10, pow, ...) don't need their own op:
// they're built by composing these primitives plus existing number_* calls
// (e.g. tan(x) = sin(x)/cos(x) is just an IRR_DIV of two IRR_SIN/IRR_COS
// results). The DAG is acyclic: an operation only ever references
// already-built operands.
enum {
    IRR_SIN, IRR_COS, IRR_EXP, IRR_LN, IRR_ATAN, IRR_SQRT,
    IRR_ADD, IRR_SUB, IRR_MUL, IRR_DIV,
};

typedef struct {
    num_head head;
    uint32_t op : 4;        // one of the IRR_* ops above (10 values, needs unsigned 4 bits)
    int32_t appr_sign : 2;  // sign of the memoized approximation; 0 until first evaluated
    number x;                 // operand (owned); LHS for binary ops
    number y;                 // RHS for ADD/SUB/MUL/DIV (owned); small_zero() (unused) for unary ops
    mpz_t appr;                // memoized |value| * 2^appr_prec
    uint32_t appr_prec;        // bits of precision in appr; 0 = no memo yet
                               // (stored precs always carry +64); unbounded, full-width
} num_irr;

static inline num_head *number_head(number x) { return (num_head *)(uintptr_t)x.bits; }
static inline num_bigrat *number_heap(number x) { return (num_bigrat *)(uintptr_t)x.bits; }
static inline num_real *as_real(number x) { return (num_real *)(uintptr_t)x.bits; }
static inline num_irr *as_irr(number x) { return (num_irr *)(uintptr_t)x.bits; }

static inline bool is_real_kind(number x)
{
    return number_tag(x) == TAG_POINTER && x.bits != 0 && number_head(x)->kind == KIND_REAL;
}

static inline bool is_irrational_kind(number x)
{
    return number_tag(x) == TAG_POINTER && x.bits != 0 && number_head(x)->kind == KIND_IRRATIONAL;
}

// Past this many bits of requested precision, an approximation attempt gives
// up rather than loop forever — the only way a refinement can fail to
// terminate is a general IRRATIONAL value that is (or is suspiciously close
// to) exactly zero without any symbolic proof either way, e.g.
// sin(x)^2+cos(x)^2-1, which is mathematically zero but unrecognized by any
// identity this design knows. ~5k decimal digits: generous for any
// calculator use, but bounded -- and needs to stay fairly low, not just
// "safely below overflow": machin_pi-style series cost still grows
// superlinearly with precision. The timings that set this value (2^14 well
// under a second; 2^16 ~7s; 2^18 unfinished in 40s) were measured on the
// old portable schoolbook backend; GMP's subquadratic multiplication moves the
// wall substantially, so re-measure if this threshold is ever revisited. A
// give-up is meant to be a rare, cheap dead end, not a multi-second hang.
#define IRR_MAX_PREC (1u << 14)

// Tier 3 forward declarations (implementations after the rational tiers).
static number real_add(number x, number y);
static number real_mul(number x, number y);
static number real_div(number x, number y);
static char *real_to_string(num_real *r, uint32_t max_frac_digits);
// Sign of a real/irrational value via interval refinement; false if the
// value can't be resolved within IRR_MAX_PREC bits (see the macro above).
static bool refine_sign(number x, int *sign_out);
// Builds a general IRRATIONAL node combining x and y (y is small_zero(),
// i.e. unused, for unary ops), taking ownership of both. For IRR_DIV, first
// establishes y is nonzero via refine_sign; gives up -> NUMBER_ERROR.
static number make_irr(int op, number x, number y);
// Any non-error number as fixed-point at precision w, |error| <= 4. False
// if the approximation can't be resolved (only possible for an IRRATIONAL
// value; see IRR_MAX_PREC).
static bool value_fixed(number x, uint32_t w, int *sign_out, bigint **mag_out);
static bool irr_fixed(num_irr *n, uint32_t w, int *sign_out, bigint **mag_out);
// exp(x)/ln(x) * 2^w as sign/magnitude, |error| <= 4 -- the numeric core
// shared by irr_fixed's IRR_EXP/IRR_LN cases (x arbitrary) and factor_appr's
// FN_EXP/FN_LN cases (x always a plain rational, so these never actually
// fail there). False only if x's own approximation can't be resolved.
static bool exp_fixed_core(number x, uint32_t w, int *sign_out, bigint **mag_out);
static bool ln_fixed_core(number x, uint32_t w, int *sign_out, bigint **mag_out);
static int signed_add(int s1, bigint *m1, int s2, bigint *m2, bigint **out);

// Wrap a canonical, known-not-small mpq into a fresh heap bigrat, MOVING
// q's contents (struct copy; the limb buffers change owner and q must not
// be used or cleared afterwards).
static number bigrat_steal(mpq_t q)
{
    num_bigrat *p = xmalloc(sizeof(num_bigrat));
    p->head.kind = KIND_BIGRAT;
    p->q[0] = q[0];
    return (number){(uint64_t)(uintptr_t)p};
}

// Allocate a fresh heap bigrat with an initialized (zero) embedded mpq, ready
// to fill in place. Lets the u128/u64 constructors below write num/den
// straight into the final allocation instead of building a throwaway pair of
// heap-mpz shells and swapping their limbs in.
static num_bigrat *bigrat_alloc(void)
{
    num_bigrat *p = xmalloc(sizeof(num_bigrat));
    p->head.kind = KIND_BIGRAT;
    mpq_init(p->q);
    return p;
}

// Build a heap bigrat directly from a sign and two already-reduced magnitudes
// (num, den coprime, den > 0, value known not to fit the small tier), writing
// into the embedded mpq with no intermediate bigint shells. u128 covers the
// u64 callers too (a widened u64 takes mpz_set_u128's single-word fast path).
static number bigrat_from_u128s(int sign, u128 num, u128 den)
{
    num_bigrat *p = bigrat_alloc();
    mpz_set_u128(mpq_numref(p->q), num);
    mpz_set_u128(mpq_denref(p->q), den);
    if (sign < 0) mpz_neg(mpq_numref(p->q), mpq_numref(p->q));
    return (number){(uint64_t)(uintptr_t)p};
}

// Consume a CANONICAL mpq (mpq_canonicalize invariants already hold) and
// return it as a number: the small immediate if it fits, else a heap bigrat.
static number number_from_mpq(mpq_t q)
{
    if (mpq_sgn(q) == 0) {
        mpq_clear(q);
        return small_zero();
    }
    if (mpz_cmpabs_ui(mpq_numref(q), SMALL_NUM_MAX) <= 0 &&
        mpz_cmp_ui(mpq_denref(q), SMALL_DEN_MAX) <= 0) {
        int32_t n = (int32_t)mpz_get_si(mpq_numref(q));
        uint32_t d = (uint32_t)mpz_get_ui(mpq_denref(q));
        mpq_clear(q);
        return small_make(n, d);
    }
    return bigrat_steal(q);
}

// Canonicalize sign * num/den into a number, taking ownership of num and den:
// reduce by the gcd, then demote to a small rational if it fits.
static number canon_make(int sign, bigint *num, bigint *den)
{
    if (bi_is_zero(den)) {
        bi_free(num);
        bi_free(den);
        return err(ERR_DIV_BY_ZERO);
    }
    mpq_t q;
    mpq_init(q);
    mpz_swap(mpq_numref(q), num);
    mpz_swap(mpq_denref(q), den);
    if (sign < 0) mpz_neg(mpq_numref(q), mpq_numref(q));
    bi_free(num);
    bi_free(den);
    mpq_canonicalize(q);
    return number_from_mpq(q);
}

// ---------------------------------------------------------------------------
// Tier 2 u128 fast path
//
// A number reaches tier2 once it no longer fits the small immediate's
// 31-bit numerator / 30-bit denominator, but its magnitude very commonly
// still fits a plain uint64_t -- the immediate form's fields are narrower
// than the machine word for tagging reasons (see number.h), not because
// values that size are rare. When both operands' num/den fit u64, every
// intermediate of add/sub/mul/div fits in a single u128 (products of two
// u64s never exceed ~2^128, and the one addition that could theoretically
// carry past that -- unlike-sign-free add/sub's cross-term sum -- is
// overflow-checked and falls back on the rare operand pair large enough to
// trip it). That keeps the whole operation off the GMP heap: zero
// allocations when the reduced result re-fits a small immediate, versus
// the general mpq path's result construction (and canonicalizing gcd) per
// op. Comparison gets the same treatment: it needs no result construction,
// so the whole operation allocates nothing.

// Unpacks x's magnitude as sign/num/den with no allocation, if it fits u64.
// A zero-allocation read-only mpq view of a rational number. For a heap
// bigrat the stored mpq is returned directly (borrowed); for a small
// immediate, v is filled with read-only mpz's over its own limb storage
// (mpz_roinit_n -- no GMP allocation, nothing to clear). Valid only while
// x is live and only for read-only mpq_* arguments.
typedef struct {
    mpq_t q;
    mp_limb_t nl[1], dl[1];
} mpq_view;

static mpq_srcptr number_mpq_view(number x, mpq_view *v)
{
    if (number_tag(x) == TAG_POINTER) return number_heap(x)->q;
    int64_t n = small_num(x);
    uint64_t un = n < 0 ? (uint64_t)-n : (uint64_t)n;
    v->nl[0] = un;
    v->dl[0] = small_den(x);
    mpz_roinit_n(mpq_numref(v->q), v->nl, n < 0 ? -1 : (un != 0));
    mpz_roinit_n(mpq_denref(v->q), v->dl, 1);
    return v->q;
}

// Always succeeds for TAG_SMALL; for a heap bigrat, only if its num and den
// (already reduced/canonical) each still fit 64 bits.
static bool unpack_u64_rat(number x, int *sign, uint64_t *num, uint64_t *den)
{
    if (number_tag(x) == TAG_SMALL) {
        int64_t n = small_num(x);
        *sign = n < 0 ? -1 : 1;
        *num = n < 0 ? (uint64_t)-n : (uint64_t)n;
        *den = small_den(x);
        return true;
    }
    num_bigrat *p = number_heap(x);
    *sign = mpq_sgn(p->q) < 0 ? -1 : 1;
    return bi_fits_u64(mpq_numref(p->q), num) && bi_fits_u64(mpq_denref(p->q), den);
}

// Turns an ALREADY-REDUCED sign*num/den (gcd(num,den) == 1, den > 0) into a
// number: a small immediate if it fits, else a heap bigrat built directly
// into the embedded mpq (bigrat_from_u128s) -- one xmalloc plus GMP's two
// limb buffers, no throwaway bigint shells. num == 0 canonicalizes to zero
// regardless of den (callers that can produce a cancellation reach here with
// den == 1 anyway, but the guard keeps this correct for any caller).
static number finish_reduced_u128(int sign, u128 num, u128 den)
{
    if (num == 0) return small_zero();
    if (num <= SMALL_NUM_MAX && den <= SMALL_DEN_MAX) {
        int32_t n32 = (int32_t)num;
        return small_make(sign < 0 ? -n32 : n32, (uint32_t)den);
    }
    return bigrat_from_u128s(sign, num, den);
}

// Attempts add/sub entirely in u64/u128; false means an operand didn't fit
// u64 or the cross-term sum overflowed u128, and the caller should fall
// back to the general bigint path. bsign flips b's sign for subtraction.
//
// Reduces denominators first, the way GMP's mpq_aors does, rather than
// forming the raw cross-product and reducing after. With g = gcd(da, db):
// the common denominator is lcm = da*(db/g), and the summed numerator is
// na*(db/g) +/- nb*(da/g). Because both operands are already canonical, the
// only factor the result can still share is one dividing g (na*(db/g) is
// coprime to da/g and to db/g, likewise nb*(da/g)); so the leftover
// reduction is gcd(num, g), NOT a fresh gcd against the full ~126-bit den.
// In the common case gcd(da,db) == 1 (~61% of random pairs) there is no
// leftover factor at all: the result is canonical with zero reduction work,
// where an unconditional reduce-the-full-product approach would pay a 126-bit
// binary GCD plus two software u128 divisions on every add. This is the bulk
// of the large-fraction add/sub cost, and the reason mpq_add outran number.
static bool rat_addsub_u128(number a, number b, int bsign, number *out)
{
    int sa, sb;
    uint64_t na, da, nb, db;
    if (!unpack_u64_rat(a, &sa, &na, &da) || !unpack_u64_rat(b, &sb, &nb, &db))
        return false;
    sb *= bsign;
    uint64_t g = gcd_u64(da, db);
    uint64_t da_r = da / g, db_r = db / g; // da_r == da, db_r == db when g == 1
    u128 t1 = (u128)na * db_r, t2 = (u128)nb * da_r; // each a single product: fits u128
    u128 den = (u128)da * db_r; // lcm(da, db); <= the old da*db, so no more prone to overflow
    u128 num;
    int sign;
    if (sa == sb) {
        // The only op in this fast path where two u128 products are summed
        // rather than one taken alone -- the one place a 64-bit operand pair
        // can still overflow u128 (each term can be within a factor of 2 of
        // 2^128), so this is the one checked add.
        if (__builtin_add_overflow(t1, t2, &num)) return false;
        sign = sa;
    } else if (t1 >= t2) {
        num = t1 - t2;
        sign = sa;
    } else {
        num = t2 - t1;
        sign = sb;
    }
    if (g != 1) {
        // Strip the only factor num/den can still share (a divisor of g).
        u128 g2 = gcd_u128(num, g);
        num /= g2;
        den /= g2;
    }
    *out = finish_reduced_u128(sign, num, den);
    return true;
}

// Attempts multiplication entirely in u64/u128; false means an operand
// didn't fit u64.
//
// Cross-cancels before multiplying, the way GMP's mpq_mul does, rather than
// forming na*nb / da*db and reducing after. The result na*nb/(da*db) can only
// share a factor across the two diagonals -- gcd(na,db) and gcd(nb,da) -- since
// gcd(na,da) and gcd(nb,db) are already 1 (canonical operands). Dividing those
// out first leaves a product that is coprime by construction: canonical with
// NO final ~126-bit gcd, and with smaller multiplicands. Each of num/den is
// still a single u64*u64 product (the reduced factors are <= the originals),
// so there is no overflow case to guard.
static bool rat_mul_u128(number a, number b, number *out)
{
    int sa, sb;
    uint64_t na, da, nb, db;
    if (!unpack_u64_rat(a, &sa, &na, &da) || !unpack_u64_rat(b, &sb, &nb, &db))
        return false;
    // Denominators are >= 1, so both gcds are >= 1 (no zero divide); na == 0
    // gives g1 == db, na/g1 == 0 -> finish_reduced_u128's num==0 zero guard.
    uint64_t g1 = gcd_u64(na, db), g2 = gcd_u64(nb, da);
    u128 num = (u128)(na / g1) * (nb / g2);
    u128 den = (u128)(da / g2) * (db / g1);
    *out = finish_reduced_u128(sa * sb, num, den);
    return true;
}

// Attempts division entirely in u64/u128; false means an operand didn't fit
// u64. Caller has already excluded b == 0.
//
// a/b = (na*db)/(da*nb) -- multiplication by the reciprocal -- so the same
// cross-cancel as rat_mul_u128 applies, on the diagonals gcd(na,nb) and
// gcd(db,da); the other two pairs (na,da) and (db,nb) are already coprime.
static bool rat_div_u128(number a, number b, number *out)
{
    int sa, sb;
    uint64_t na, da, nb, db;
    if (!unpack_u64_rat(a, &sa, &na, &da) || !unpack_u64_rat(b, &sb, &nb, &db))
        return false;
    uint64_t g1 = gcd_u64(na, nb), g2 = gcd_u64(db, da); // nb >= 1 (b != 0), da/db >= 1
    u128 num = (u128)(na / g1) * (db / g2);
    u128 den = (u128)(nb / g1) * (da / g2);
    *out = finish_reduced_u128(sa * sb, num, den);
    return true;
}

// Attempts a same-sign magnitude comparison (|a| vs |b|, both already known
// rational) entirely in u64/u128; false means an operand didn't fit u64.
// Unlike addsub, a single cross product each side is all this needs -- no
// sum, so no overflow case to guard.
static bool rat_compare_u128(number a, number b, int sa, int *out)
{
    int sa_unused, sb_unused;
    uint64_t na, da, nb, db;
    if (!unpack_u64_rat(a, &sa_unused, &na, &da) || !unpack_u64_rat(b, &sb_unused, &nb, &db))
        return false;
    u128 l = (u128)na * db, r = (u128)nb * da;
    *out = ((l > r) - (l < r)) * sa;
    return true;
}

// Build a number from an int64 ratio (the workhorse for all small-value
// construction). Handles sign, reduction, and promotion to big rationals.
static number number_from_ratio64(int64_t num, int64_t den)
{
    if (den == 0) return err(ERR_DIV_BY_ZERO);
    int sign = 1;
    uint64_t un = num < 0 ? (sign = -sign, -(uint64_t)num) : (uint64_t)num;
    uint64_t ud = den < 0 ? (sign = -sign, -(uint64_t)den) : (uint64_t)den;
    if (un == 0) return small_zero();
    uint64_t g = gcd_u64(un, ud);
    un /= g;
    ud /= g;
    if (un <= SMALL_NUM_MAX && ud <= SMALL_DEN_MAX) {
        int32_t n32 = (int32_t)un;
        return small_make(sign < 0 ? -n32 : n32, (uint32_t)ud);
    }
    NSTAT(promotions);
    return bigrat_from_u128s(sign, un, ud);
}

// ---------------------------------------------------------------------------
// Constructors

number number_from_int(int64_t value) { return number_from_ratio64(value, 1); }

number number_from_ratio(int64_t numerator, int64_t denominator)
{
    return number_from_ratio64(numerator, denominator);
}

number number_from_double(double value)
{
    if (!isfinite(value)) return err(ERR_NOT_FINITE);
    if (value == 0) return small_zero();
    int sign = value < 0 ? -1 : 1;
    int e;
    double m = frexp(fabs(value), &e); // m in [0.5, 1)
    uint64_t mant = (uint64_t)ldexp(m, 53); // exact 53-bit integer
    int p2 = e - 53; // value = mant * 2^p2

    // Trim mant's trailing zero bits into p2 up front -- exactly what the
    // gcd-based bigint path below would eventually reduce down to, but
    // doing it in pure integer ops first lets a "round" double (0.5, 1.25,
    // 100.0, ...) take a heap-free int64 fast path instead of always
    // building through bi_shl/bi_from_u64 regardless of how few
    // significant bits it actually has. Shift guards (p2 < 63 / -p2 < 63)
    // avoid UB from shifting a 64-bit value by >= its width; number_from_
    // ratio64 handles its own further reduction and tier placement, so
    // this only needs to avoid overflowing the int64 num/den it's handed.
    if (mant != 0) {
        int tz = __builtin_ctzll(mant);
        mant >>= tz;
        p2 += tz;
    }
    if (p2 >= 0) {
        if (p2 < 63 && mant <= (uint64_t)INT64_MAX >> p2)
            return number_from_ratio64(sign * (int64_t)(mant << p2), 1);
    } else if (-p2 < 63) {
        return number_from_ratio64(sign * (int64_t)mant, (int64_t)1 << -p2);
    }

    // Out of int64 range: mpq_set_d is the same exact conversion (mantissa
    // times a power of two, trailing zeros trimmed), producing a canonical
    // mpq directly.
    mpq_t q;
    mpq_init(q);
    mpq_set_d(q, value);
    return number_from_mpq(q);
}

// ---------------------------------------------------------------------------
// Predicates

bool number_is_error(number x) { return number_tag(x) == TAG_ERROR; }

const char *number_error_message(number x)
{
    if (!number_is_error(x)) return NULL;
    uint64_t code = x.bits >> 2;
    return ERROR_MESSAGES[code < ERR_COUNT ? code : ERR_GENERIC];
}

bool number_is_zero(number x) { return x.bits == small_zero().bits; }

int number_sign(number x)
{
    switch (number_tag(x)) {
    case TAG_SMALL: {
        int64_t n = small_num(x);
        return (n > 0) - (n < 0);
    }
    case TAG_POINTER: {
        uint32_t kind = number_head(x)->kind;
        if (kind == KIND_REAL || kind == KIND_IRRATIONAL) {
            int s;
            // A real is always nonzero (irrational); an irrational node
            // might not resolve within the cap, in which case treat it as
            // indistinguishable from zero (number_sign has no error return).
            return refine_sign(x, &s) ? s : 0;
        }
        return mpq_sgn(number_heap(x)->q);
    }
    default: return 0;
    }
}

bool number_is_negative(number x) { return number_sign(x) < 0; }
bool number_is_rational(number x)
{
    return !number_is_error(x) && !is_real_kind(x) && !is_irrational_kind(x);
}
bool number_is_integer(number x)
{
    if (number_tag(x) == TAG_SMALL) return small_den(x) == 1;
    // Reals are irrational by construction; an IRRATIONAL node can't be
    // proven integer (see the header comment: false means "not provably").
    if (number_tag(x) != TAG_POINTER || x.bits == 0 || number_head(x)->kind != KIND_BIGRAT)
        return false;
    return bi_is_one(mpq_denref(number_heap(x)->q));
}

// ---------------------------------------------------------------------------
// Comparison

int number_compare_general(number a, number b)
{
    NSTAT(ops[STAT_COMPARE]);
    if (number_is_error(a) || number_is_error(b)) return 2;
    if (a.bits == b.bits) return 0; // same immediate, or the same shared heap reference
    if (number_tag(a) == TAG_SMALL && number_tag(b) == TAG_SMALL) {
        NSTAT(tier1_general);
        // Cross-multiply: |num| <= 2^31, den < 2^30, so products fit in int64.
        int64_t l = small_num(a) * (int64_t)small_den(b);
        int64_t r = small_num(b) * (int64_t)small_den(a);
        return (l > r) - (l < r);
    }
    if (is_real_kind(a) || is_real_kind(b) || is_irrational_kind(a) || is_irrational_kind(b)) {
        NSTAT(tier3);
        // number_sub always succeeds for non-error operands here (symbolic
        // forms that don't unify fall back to a general IRRATIONAL node
        // rather than erroring): if the difference simplifies to an exact
        // rational/real symbolic form, its sign decides exactly. Otherwise
        // it needs refine_sign directly (NOT the public number_sign, whose
        // give-up fallback of 0 would be misread here as "equal" -- this is
        // the one place that distinction matters: refine_sign only ever
        // succeeds by proving nonzero, never by proving exactly zero, so a
        // give-up here means "can't decide", which is unordered (2), not
        // equal (0)).
        number diff = number_sub(a, b);
        int result;
        if (is_real_kind(diff) || is_irrational_kind(diff)) {
            int s;
            result = refine_sign(diff, &s) ? s : 2;
        } else {
            result = number_sign(diff);
        }
        return result;
    }
    int sa = number_sign(a), sb = number_sign(b);
    if (sa != sb) return sa < sb ? -1 : 1;
    if (sa == 0) return 0;
    NSTAT(tier2);
    int fast;
    if (rat_compare_u128(a, b, sa, &fast)) {
        NSTAT(tier2_u128_fast);
        return fast;
    }
    mpq_view va, vb;
    return mpq_cmp(number_mpq_view(a, &va), number_mpq_view(b, &vb));
}
int number_compare(number a, number b) { return number_compare_general(a, b); } // see comment above number_add_general

bool number_equal_general(number a, number b)
{
    if (number_is_error(a) || number_is_error(b)) return false;
    if (a.bits == b.bits) return true; // same immediate, or the same shared heap reference
    if (is_real_kind(a) || is_real_kind(b) || is_irrational_kind(a) || is_irrational_kind(b))
        return number_compare(a, b) == 0; // exact, incl. sqrt(8) == 2*sqrt(2)
    // Canonical forms make equality structural: two equal values have
    // identical representations (immediates are bit-equal; a value never
    // exists as both a small and a big rational).
    if (number_tag(a) != number_tag(b)) return false;
    if (number_tag(a) == TAG_SMALL) return a.bits == b.bits;
    return mpq_equal(number_heap(a)->q, number_heap(b)->q) != 0;
}
bool number_equal(number a, number b) { return number_equal_general(a, b); } // see comment above number_add_general

// ---------------------------------------------------------------------------
// Arithmetic

static number rat_addsub_general(number a, number b, int bsign)
{
    mpq_view va, vb;
    mpq_srcptr qa = number_mpq_view(a, &va), qb = number_mpq_view(b, &vb);
    mpq_t r;
    mpq_init(r);
    if (bsign < 0) mpq_sub(r, qa, qb);
    else mpq_add(r, qa, qb);
    return number_from_mpq(r); // mpq results are canonical
}

static number number_addsub(number a, number b, int bsign)
{
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    if (is_irrational_kind(a) || is_irrational_kind(b) || is_real_kind(a) || is_real_kind(b)) {
        NSTAT(tier3);
        number bb = bsign < 0 ? number_neg(b) : b;
        // Try the exact symbolic form first (real_add never sees an
        // IRRATIONAL operand: it doesn't know how to, since !is_real_kind()
        // means "treat as rational" there). Only on failure — no closed form
        // unifies them, or an operand is already IRRATIONAL — fall back to
        // a general node.
        number result = (is_irrational_kind(a) || is_irrational_kind(bb)) ?
                             NUMBER_ERROR : real_add(a, bb);
        if (number_is_error(result))
            result = make_irr(IRR_ADD, a, bb);
        return result;
    }
    if (number_tag(a) == TAG_SMALL && number_tag(b) == TAG_SMALL) {
        int64_t n1 = small_num(a), n2 = bsign * small_num(b);
        uint64_t d1 = small_den(a), d2 = small_den(b);
        if (d1 == 1 && d2 == 1) { // integer sub-path: no gcd needed
            NSTAT(tier1_fastpath);
            return number_from_ratio64(n1 + n2, 1);
        }
        NSTAT(tier1_general);
        // Raw cross-multiply, no pre-reduction: |each num term| <=
        // (2^31-1)(2^30-1) < 2^61, so |their sum| < 2^62, and den <
        // (2^30-1)^2 < 2^60 -- everything fits int64 with 2x headroom.
        // An earlier version gcd-reduced the denominators first (the
        // classic trick for keeping intermediates small), but at these
        // sizes the intermediates were never in danger, and it meant two
        // full gcd passes per add -- this one and number_from_ratio64's
        // canonicalizing one. number_mul/number_div's fraction paths were
        // already shaped this way (raw product, single reduction in
        // number_from_ratio64); measured, this matches: dropping the
        // pre-gcd took fraction add/sub from ~2x mul's cost to parity.
        int64_t num = n1 * (int64_t)d2 + n2 * (int64_t)d1;
        int64_t den = (int64_t)(d1 * d2);
        return number_from_ratio64(num, den);
    }
    NSTAT(tier2);
    number fast;
    if (rat_addsub_u128(a, b, bsign, &fast)) {
        NSTAT(tier2_u128_fast);
        return fast;
    }
    return rat_addsub_general(a, b, bsign);
}

number number_add_general(number a, number b) { NSTAT(ops[STAT_ADD]); return number_addsub(a, b, 1); }
number number_sub_general(number a, number b) { NSTAT(ops[STAT_SUB]); return number_addsub(a, b, -1); }
// Plain (non-inline) bodies: the GNU89 "true definition" number.h's
// extern-inline fast-path dispatchers fall back on when they don't inline
// a call (address-of; also every call, in a -DNUMBER_STATS build, where
// number.h deliberately doesn't shadow these names with an inline version
// at all -- see number.h's comment above number_add_general).
number number_add(number a, number b) { return number_add_general(a, b); }
number number_sub(number a, number b) { return number_sub_general(a, b); }

number number_mul_general(number a, number b)
{
    NSTAT(ops[STAT_MUL]);
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    if (is_irrational_kind(a) || is_irrational_kind(b) || is_real_kind(a) || is_real_kind(b)) {
        NSTAT(tier3);
        // Multiplying by exactly 1 is an identity: return the other
        // operand instead of a useless IRR_MUL node around it (int_pow's
        // accumulator starts at 1, so pow(pi, n) would otherwise carry a
        // "1 *" factor forever). A rational 1 is always the canonical
        // small immediate, so one bit-compare decides it.
        if (a.bits == NUMBER_ONE.bits) return b;
        if (b.bits == NUMBER_ONE.bits) return a;
        number result = (is_irrational_kind(a) || is_irrational_kind(b)) ?
                             NUMBER_ERROR : real_mul(a, b);
        if (number_is_error(result))
            result = make_irr(IRR_MUL, a, b);
        return result;
    }
    if (number_tag(a) == TAG_SMALL && number_tag(b) == TAG_SMALL) {
        int64_t n1 = small_num(a), n2 = small_num(b);
        uint64_t d1 = small_den(a), d2 = small_den(b);
        if (d1 == 1 && d2 == 1) { // integer sub-path
            NSTAT(tier1_fastpath);
            return number_from_ratio64(n1 * n2, 1);
        }
        NSTAT(tier1_general);
        return number_from_ratio64(n1 * n2, (int64_t)(d1 * d2));
    }
    NSTAT(tier2);
    number fast;
    if (rat_mul_u128(a, b, &fast)) {
        NSTAT(tier2_u128_fast);
        return fast;
    }
    mpq_view va, vb;
    mpq_t r;
    mpq_init(r);
    mpq_mul(r, number_mpq_view(a, &va), number_mpq_view(b, &vb));
    return number_from_mpq(r);
}
number number_mul(number a, number b) { return number_mul_general(a, b); } // see comment above number_add_general

number number_div_general(number a, number b)
{
    NSTAT(ops[STAT_DIV]);
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    if (number_is_zero(b)) return err(ERR_DIV_BY_ZERO); // catches the exact-rational-zero case
    // An exactly-zero numerator (e.g. ln(1)/ln(10)) is always 0, however
    // unsimplified b is -- short-circuits refine_sign ever needing to prove
    // an already-known-zero value nonzero all the way out to IRR_MAX_PREC.
    if (number_is_zero(a)) return small_zero();
    if (is_irrational_kind(a) || is_irrational_kind(b) || is_real_kind(a) || is_real_kind(b)) {
        NSTAT(tier3);
        number result = (is_irrational_kind(a) || is_irrational_kind(b)) ?
                             NUMBER_ERROR : real_div(a, b);
        if (number_is_error(result))
            // make_irr does the eager nonzero check on b for IRR_DIV.
            result = make_irr(IRR_DIV, a, b);
        return result;
    }
    if (number_tag(a) == TAG_SMALL && number_tag(b) == TAG_SMALL) {
        NSTAT(tier1_general); // no denominator-1 fast path here: always cross-multiplies
        // a/b = (n1*d2) / (d1*n2); both products fit in int64.
        int64_t num = small_num(a) * (int64_t)small_den(b);
        int64_t den = (int64_t)small_den(a) * small_num(b);
        return number_from_ratio64(num, den);
    }
    NSTAT(tier2);
    number fast;
    if (rat_div_u128(a, b, &fast)) {
        NSTAT(tier2_u128_fast);
        return fast;
    }
    mpq_view va, vb;
    mpq_t r;
    mpq_init(r);
    mpq_div(r, number_mpq_view(a, &va), number_mpq_view(b, &vb));
    return number_from_mpq(r);
}
number number_div(number a, number b) { return number_div_general(a, b); } // see comment above number_add_general

static number make_real(number a, number b, uint32_t fn, number arg);

number number_neg_general(number x)
{
    if (number_is_error(x)) return x;
    if (number_tag(x) == TAG_SMALL)
        return number_from_ratio64(-small_num(x), (int64_t)small_den(x));
    if (is_real_kind(x)) {
        num_real *r = as_real(x);
        return make_real(number_neg(r->a), number_neg(r->b), r->fn,
                         r->arg);
    }
    if (is_irrational_kind(x)) {
        // Not number_sub(small_zero(), x): that's subtraction with bsign<0,
        // which calls number_neg on its second operand -- straight back
        // here, infinitely. Negate via multiplication instead.
        number neg1 = number_from_int(-1);
        number result = number_mul(x, neg1);
        return result;
    }
    num_bigrat *p = number_heap(x);
    mpq_t r;
    mpq_init(r);
    mpq_neg(r, p->q); // same magnitude: still not small, no demote check
    return bigrat_steal(r);
}
number number_neg(number x) { return number_neg_general(x); } // see comment above number_add_general

number number_abs(number x)
{
    if (number_is_error(x)) return x;
    return number_is_negative(x) ? number_neg(x) : x;
}

number number_inverse(number x)
{
    if (number_is_error(x)) return x;
    if (is_real_kind(x) || is_irrational_kind(x)) {
        number one = number_from_int(1);
        number res = number_div(one, x); // public dispatcher: handles both kinds
        return res;
    }
    if (number_tag(x) == TAG_SMALL)
        return number_from_ratio64((int64_t)small_den(x), small_num(x));
    num_bigrat *p = number_heap(x);
    mpq_t r;
    mpq_init(r);
    mpq_inv(r, p->q); // canonical in, canonical out; may now fit small
    return number_from_mpq(r);
}

// Greatest common divisor and least common multiple, generalized to rationals:
// gcd is the largest g >= 0 such that a/g and b/g are both integers; lcm is the
// smallest l >= 0 that is an integer multiple of both. For integer operands
// these are the ordinary gcd/lcm; for reduced fractions pa/qa, pb/qb they are
//   gcd = gcd(pa, pb) / lcm(qa, qb)   lcm = lcm(pa, pb) / gcd(qa, qb)
// (so e.g. gcd(1/2, 1/3) == 1/6 and lcm(1/2, 1/3) == 1). Both results are
// non-negative regardless of operand signs. Following the usual conventions,
// gcd(0, x) == |x| (gcd(0, 0) == 0) and lcm(0, x) == 0. An irrational operand
// has no such divisor structure -> error; an error operand propagates.

number number_gcd(number a, number b)
{
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    if (!number_is_rational(a) || !number_is_rational(b)) return err(ERR_GCD_IRRATIONAL);
    if (number_is_zero(a)) return number_abs(b);
    if (number_is_zero(b)) return number_abs(a);
    mpq_view va, vb;
    mpq_srcptr qa = number_mpq_view(a, &va), qb = number_mpq_view(b, &vb);
    mpq_t r;
    mpq_init(r);
    mpz_gcd(mpq_numref(r), mpq_numref(qa), mpq_numref(qb)); // sign-blind, result >= 0
    mpz_lcm(mpq_denref(r), mpq_denref(qa), mpq_denref(qb));
    mpq_canonicalize(r); // gcd(na,nb)/lcm(da,db) is in fact already reduced; keep the invariant explicit
    return number_from_mpq(r);
}

number number_lcm(number a, number b)
{
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    if (!number_is_rational(a) || !number_is_rational(b)) return err(ERR_GCD_IRRATIONAL);
    if (number_is_zero(a) || number_is_zero(b)) return small_zero();
    mpq_view va, vb;
    mpq_srcptr qa = number_mpq_view(a, &va), qb = number_mpq_view(b, &vb);
    mpq_t r;
    mpq_init(r);
    mpz_lcm(mpq_numref(r), mpq_numref(qa), mpq_numref(qb)); // sign-blind, result >= 0
    mpz_gcd(mpq_denref(r), mpq_denref(qa), mpq_denref(qb));
    mpq_canonicalize(r);
    return number_from_mpq(r);
}

// ---------------------------------------------------------------------------
// Rounding to an integer: floor/ceil/trunc/round, and the floored modulus
// built on floor. The rational tiers round directly (one bigint divmod at
// most); irrational values refine [x - err, x + err] until both interval
// ends round to the same integer -- see tier3_to_int.

enum { INT_FLOOR, INT_CEIL, INT_TRUNC, INT_ROUND };

// floor of a rational, non-error x.
static number rat_floor(number x)
{
    if (number_tag(x) == TAG_SMALL) {
        int64_t n = small_num(x), d = (int64_t)small_den(x);
        int64_t q = n / d;
        if (n % d != 0 && n < 0) q--; // C truncates toward zero; floor doesn't
        return number_from_ratio64(q, 1);
    }
    num_bigrat *p = number_heap(x);
    if (bi_is_one(mpq_denref(p->q))) return x; // already an integer
    mpq_t r;
    mpq_init(r);
    mpz_fdiv_q(mpq_numref(r), mpq_numref(p->q), mpq_denref(p->q)); // true floor
    return number_from_mpq(r); // den stays 1: canonical
}

// q must be integer-valued (denominator 1; either rational tier).
static bool int_is_even(number q)
{
    if (number_tag(q) == TAG_SMALL) return (small_num(q) & 1) == 0;
    return mpz_even_p(mpq_numref(number_heap(q)->q));
}

// A rational, non-error x rounded to an integer-valued number by mode.
static number rat_to_int(number x, int mode)
{
    switch (mode) {
    case INT_FLOOR: return rat_floor(x);
    case INT_CEIL: { // ceil(x) = -floor(-x)
        number nx = number_neg(x);
        number f = rat_floor(nx);
        number res = number_neg(f);
        return res;
    }
    case INT_TRUNC:
        return rat_to_int(x, number_is_negative(x) ? INT_CEIL : INT_FLOOR);
    default: { // INT_ROUND: nearest, ties to even
        number q = rat_floor(x);
        number frac = number_sub(x, q); // in [0, 1)
        int c = number_compare(frac, NUMBER_SMALL(1, 2));
        if (c < 0 || (c == 0 && int_is_even(q))) return q;
        number q1 = number_add(q, NUMBER_ONE); // the tie's other neighbor is the even one
        return q1;
    }
    }
}

// An irrational x rounded to an integer by mode: refine x's approximation
// until the whole interval it pins x inside rounds to a single integer.
// Terminates as soon as x's distance from the nearest rounding boundary
// exceeds the approximation error -- guaranteed eventually for any value
// not exactly ON a boundary, so only a general IRRATIONAL node sitting
// unprovably on one (or an unresolvable one) reaches the precision cap and
// gives up.
static number tier3_to_int(number x, int mode)
{
    for (uint32_t w = 32; w <= IRR_MAX_PREC; w *= 2) {
        int s;
        bigint *v;
        if (!value_fixed(x, w, &s, &v)) break;
        // x lies within [s*v - 4, s*v + 4] / 2^w (value_fixed's error bound).
        bigint *lom, *him;
        int los = signed_add(s, bi_copy(v), -1, bi_from_u64(4), &lom);
        int his = signed_add(s, v, 1, bi_from_u64(4), &him);
        bigint *unit = bi_from_u64(1); // bi_shl doesn't consume its input
        number lo = canon_make(los, lom, bi_shl(unit, w));
        number hi = canon_make(his, him, bi_shl(unit, w));
        bi_free(unit);
        number il = rat_to_int(lo, mode), ih = rat_to_int(hi, mode);
        bool decided = number_equal(il, ih);
        if (decided) return il;
    }
    return err(ERR_UNDECIDABLE_INT);
}

static number number_to_int(number x, int mode)
{
    if (number_is_error(x)) return x;
    if (is_real_kind(x) || is_irrational_kind(x)) return tier3_to_int(x, mode);
    return rat_to_int(x, mode);
}

number number_floor(number x) { return number_to_int(x, INT_FLOOR); }
number number_ceil(number x) { return number_to_int(x, INT_CEIL); }
number number_trunc(number x) { return number_to_int(x, INT_TRUNC); }
number number_round(number x) { return number_to_int(x, INT_ROUND); }

number number_mod(number a, number b)
{
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    if (number_is_zero(b)) return err(ERR_DIV_BY_ZERO);
    number q = number_div(a, b);
    number qi = number_floor(q);
    if (number_is_error(qi)) return qi; // undecidable floor (or IRR division failure)
    number prod = number_mul(qi, b);
    number res = number_sub(a, prod);
    return res;
}

// Decide a < b (-1), a == b (0), or a > b (+1) by comparing a and b each
// rounded to `digits` fractional digits -- always total and terminating.
// number_min/number_max use this only when exact comparison gives up (two
// general irrationals indistinguishable within the precision cap); ties (and
// the pathological case where even the rounding fails to resolve) report 0.
static int compare_rounded(number a, number b, uint32_t digits)
{
    number scale = number_pow(number_from_int(10), number_from_int((int64_t)digits));
    number as = number_mul(a, scale);
    number bs = number_mul(b, scale);
    number ra = number_round(as);
    number rb = number_round(bs);
    int c = (number_is_error(ra) || number_is_error(rb)) ? 0 : number_compare(ra, rb);
    return c == 2 ? 0 : c; // 2 (unordered) can't arise from two integers, but be safe
}

// The smaller (number_min) / larger (number_max) of a and b, returned as the
// exact original value (not a rounded one) -- a fresh reference the caller
// owns. Exact comparison decides the common case; only for two general
// irrationals whose order that cannot resolve (see number_compare) does the
// decision fall back to comparing them rounded to `digits` fractional digits,
// which guarantees a definite, terminating answer. On a tie (including an
// exact equality, or indistinguishability at `digits`) the first operand is
// returned. An error operand propagates.
number number_min(number a, number b, uint32_t digits)
{
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    int c = number_compare(a, b);
    if (c == 2) c = compare_rounded(a, b, digits);
    return c <= 0 ? a : b;
}

number number_max(number a, number b, uint32_t digits)
{
    if (number_is_error(a)) return a;
    if (number_is_error(b)) return b;
    int c = number_compare(a, b);
    if (c == 2) c = compare_rounded(a, b, digits);
    return c >= 0 ? a : b;
}

// ---------------------------------------------------------------------------
// Tier 3: irrational reals as linear forms a + b*F
//
// F is pi or sqrt(n) with n a positive non-square integer. Key facts that
// keep the symbolic algebra exact and total:
//   - sqrt(n) and sqrt(m) are commensurable (rational multiples of each
//     other) iff n*m is a perfect square; then sqrt(m) = sqrt(nm)/n * sqrt(n).
//   - If two forms are NOT commensurable (or mix pi with sqrt), their values
//     are provably distinct, so comparisons can fall back to interval
//     refinement and are guaranteed to terminate.
//   - A form with b != 0 is irrational, hence never zero and never equal to
//     any rational.

// If the nonnegative rational x is the square of a rational, produce that
// square root and return true.
static bool rational_sqrt_exact(number x, number *out)
{
    mpq_view v;
    mpq_srcptr q = number_mpq_view(x, &v); // x >= 0: caller checked the sign
    if (!mpz_perfect_square_p(mpq_numref(q)) || !mpz_perfect_square_p(mpq_denref(q)))
        return false;
    mpq_t r;
    mpq_init(r);
    mpz_sqrt(mpq_numref(r), mpq_numref(q));
    mpz_sqrt(mpq_denref(r), mpq_denref(q));
    *out = number_from_mpq(r); // roots of a coprime pair are coprime: canonical
    return true;
}

// Build a + b*F, taking ownership of a, b, and arg. Falls back to just the
// rational a when b is zero (e.g. pi - pi), or when F itself degenerates to
// a rational (ln(1) = 0, exp(0) = 1). For FN_LN, also canonicalizes arg to
// stay > 1 (0 < arg < 1 rewrites to -ln(1/arg)), keeping F = ln(arg) > 0 --
// the invariant factor_appr/dival_of_real rely on for every fn, and the one
// that lets "a real is always nonzero" (number_sign) keep holding.
static number make_real(number a, number b, uint32_t fn, number arg)
{
    if (number_is_error(a) || number_is_error(b) || number_is_error(arg)) {
        number e = number_is_error(a) ? a : (number_is_error(b) ? b : arg);
        return e;
    }
    if (fn == FN_LN) {
        int c = number_compare(arg, NUMBER_ONE);
        if (c == 0) { // ln(1) = 0: the whole b*F term vanishes
            return a;
        }
        if (c < 0) { // ln(r) = -ln(1/r) for 0 < r < 1
            number inv_arg = number_inverse(arg);
            arg = inv_arg;
            number neg_b = number_neg(b);
            b = neg_b;
        }
    } else if (fn == FN_EXP && number_is_zero(arg)) {
        // exp(0) = 1: b*F folds into the rational part.
        number sum = number_add(a, b);
        return sum;
    }
    if (number_is_zero(b)) {
        return a;
    }
    num_real *r = xmalloc(sizeof(num_real));
    r->head.kind = KIND_REAL;
    r->fn = fn & 0x7u; // caller always passes a valid FN_* tag
    r->a = a;
    r->b = b;
    r->arg = arg;
    mpz_init(r->appr); // lazy: allocates nothing until a value is stored
    r->appr_prec = 0;  // 0 = no memo yet
    return (number){(uint64_t)(uintptr_t)r};
}

// number_pi/number_tau/number_sqrt2 are process-lifetime cached singletons:
// the underlying heap object (and, once anything refines it, its best-known
// approximation -- appr/appr_prec in num_real) is built once and held
// forever, so every call after the first hands back the same object rather
// than a fresh allocation -- and callers benefit from each other's prior
// precision work instead of starting cold every time. {0} (bits == 0, the
// never-a-valid-number sentinel -- see number-design.md "Tagging") marks
// "not yet built". Plain, non-atomic lazy init, consistent with the
// single-threaded assumption elsewhere in this file, so no locking is
// needed.
number number_pi(void)
{
    static number cached = {0};
    if (cached.bits == 0) cached = make_real(small_zero(), number_from_int(1), FN_PI, small_zero());
    return cached;
}

number number_tau(void)
{
    static number cached = {0};
    if (cached.bits == 0) cached = make_real(small_zero(), number_from_int(2), FN_PI, small_zero());
    return cached;
}

number number_sqrt2(void)
{
    static number cached = {0};
    if (cached.bits == 0) cached = number_sqrt(number_from_int(2));
    return cached;
}

number number_sqrt(number x)
{
    if (number_is_error(x)) return x;
    if (is_real_kind(x) || is_irrational_kind(x)) {
        // Already irrational (pi, sqrt(n), or a general CR value): no
        // closed symbolic form is closed under nested roots, so this falls
        // back to the general engine, same as sin/cos/exp/ln/atan of an
        // irrational argument. refine_sign both rejects negative x and
        // forces a decision on its sign; an irrational value indistinguish-
        // able from zero within IRR_MAX_PREC bits gives up -> error, the
        // same "genuinely undecidable" handling used throughout this file.
        int s;
        if (!refine_sign(x, &s)) return err(ERR_UNDECIDABLE);
        if (s < 0) return err(ERR_SQRT_NEGATIVE);
        return make_irr(IRR_SQRT, x, small_zero());
    }
    int s = number_sign(x);
    if (s < 0) return err(ERR_SQRT_NEGATIVE);
    if (s == 0) return small_zero();
    number exact;
    if (rational_sqrt_exact(x, &exact)) return exact;
    // sqrt(p/q) = (1/q) * sqrt(p*q); p*q is a non-square integer here, since
    // p/q is reduced and any square p*q would make sqrt(p/q) rational.
    mpq_view v;
    mpq_srcptr q = number_mpq_view(x, &v); // x > 0 here
    mpq_t rad, co;
    mpq_init(rad);
    mpq_init(co);
    mpz_mul(mpq_numref(rad), mpq_numref(q), mpq_denref(q)); // p*q / 1
    mpz_set_ui(mpq_numref(co), 1);
    mpz_set(mpq_denref(co), mpq_denref(q)); // 1/q: gcd(1, q) == 1, canonical
    number radicand = number_from_mpq(rad);
    number coeff = number_from_mpq(co);
    return make_real(small_zero(), coeff, FN_SQRT, radicand);
}

// exp(x): 0 -> exact 1; ln(r)'s inverse (exp(ln(r)) == r) -> exact r;
// rational x -> the closed EXP(x) symbolic form; otherwise the general
// IRRATIONAL engine.
number number_exp(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return number_from_int(1);
    if (is_real_kind(x)) {
        num_real *r = as_real(x);
        if (r->fn == FN_LN && number_is_zero(r->a) && number_equal(r->b, NUMBER_ONE))
            return r->arg; // exp(ln(r)) == r, exactly
    }
    if (number_is_rational(x))
        return make_real(small_zero(), number_from_int(1), FN_EXP, x);
    return make_irr(IRR_EXP, x, small_zero());
}

// ln(x): x <= 0 -> error; 1 -> exact 0; exp(r)'s inverse (ln(exp(r)) == r)
// -> exact r; rational x -> the closed LN(x) symbolic form; otherwise the
// general engine.
number number_ln(number x)
{
    if (number_is_error(x)) return x;
    if (number_sign(x) <= 0) return err(ERR_LOG_NONPOSITIVE);
    number one = number_from_int(1);
    bool is_one = number_equal(x, one);
    if (is_one) return small_zero();
    if (is_real_kind(x)) {
        num_real *r = as_real(x);
        if (r->fn == FN_EXP && number_is_zero(r->a) && number_equal(r->b, NUMBER_ONE))
            return r->arg; // ln(exp(r)) == r, exactly
    }
    if (number_is_rational(x))
        return make_real(small_zero(), number_from_int(1), FN_LN, x);
    return make_irr(IRR_LN, x, small_zero());
}

// Whether the word-sized magnitude v is exactly base^k for some k >= 0,
// setting *out = k. Alloc-free counterpart of bi_exact_power for values that
// fit in a machine word (every TAG_SMALL magnitude is < 2^31). base == 2 is a
// single-bit test plus a count-trailing-zeros; other bases walk a short,
// fully branch-predictable multiply chain (<= 10 steps to overshoot 2^31 in
// base 10) with no wider arithmetic than the u64 accumulator.
static bool u64_exact_power(uint64_t v, unsigned base, int64_t *out)
{
    if (v == 0) return false;
    if (base == 2) {
        if ((v & (v - 1)) != 0) return false; // more than one bit set
        *out = __builtin_ctzll(v);
        return true;
    }
    int64_t k = 0;
    uint64_t p = 1;
    while (p < v) { p *= base; k++; } // v < 2^31, base >= 2: no u64 overflow
    if (p != v) return false;
    *out = k;
    return true;
}

// If x == base^n for an integer n (any sign), sets *out = n and returns true:
// base^n lands in x's numerator (n >= 0) or denominator (n < 0). Shared by
// number_log10 and number_log2 via number_log_base.
static bool exact_log_of_power(number x, unsigned base, number *out)
{
    if (number_tag(x) == TAG_SMALL) {
        // Immediate small rational, always in lowest terms: an exact power is
        // either an integer base^n (den 1, num a power of base) or a unit
        // reciprocal base^-n (num 1, den a power of base). Everything fits in
        // a word, so this whole branch is allocation-free.
        int64_t num = small_num(x);
        if (num <= 0) return false; // x <= 0: not a power of base (an error elsewhere)
        uint64_t den = small_den(x);
        int64_t k;
        if (den == 1 && u64_exact_power((uint64_t)num, base, &k)) {
            *out = number_from_int(k);
            return true;
        }
        if (num == 1 && u64_exact_power(den, base, &k)) {
            *out = number_from_int(-k);
            return true;
        }
        return false;
    }
    if (!number_is_rational(x)) return false;
    mpq_view v;
    mpq_srcptr q = number_mpq_view(x, &v);
    if (mpq_sgn(q) < 0) return false; // x <= 0: not a power of base (and an error elsewhere)
    int64_t k;
    if (bi_is_one(mpq_denref(q)) && bi_exact_power(mpq_numref(q), base, &k)) {
        *out = number_from_int(k);
        return true;
    }
    if (mpz_cmp_ui(mpq_numref(q), 1) == 0 && bi_exact_power(mpq_denref(q), base, &k)) {
        *out = number_from_int(-k);
        return true;
    }
    return false;
}

// log_base(x) = ln(x)/ln(base): same domain as ln (x <= 0 -> error). Exact
// when x is an integer power of base. Shared by number_log10 and number_log2.
static number number_log_base(number x, unsigned base)
{
    number exact;
    if (exact_log_of_power(x, base, &exact)) return exact;
    number lnx = number_ln(x);
    if (number_is_error(lnx)) return lnx;
    number b = number_from_int(base);
    number lnb = number_ln(b);
    number result = number_div(lnx, lnb);
    return result;
}

// log10(x) = ln(x)/ln(10): same domain as ln (x <= 0 -> error). Exact for
// x == 10^n, any integer n (positive, negative, or zero).
number number_log10(number x) { return number_log_base(x, 10); }

// log2(x) = ln(x)/ln(2): same domain as ln (x <= 0 -> error). Exact for
// x == 2^n, any integer n (positive, negative, or zero).
number number_log2(number x) { return number_log_base(x, 2); }

// sqrt(n)/2 as an exact REAL value.
static number half_sqrt(int64_t n)
{
    number s = number_sqrt(number_from_int(n));
    number two = number_from_int(2);
    number result = number_div(s, two);
    return result;
}

// sin0/cos0 of local*pi/12 for local in {0,2,3,4,6} -- the five standard
// angles 0, pi/6, pi/4, pi/3, pi/2. These are exactly the base angles
// reachable when k's denominator is 1, 2, 3, 4, or 6 (see
// exact_trig_of_pi_multiple): denominators that would need pi/12 itself
// (sin(pi/12) = (sqrt6-sqrt2)/4, a sum of two different sqrt-factors our
// single-F symbolic form can't hold directly) never arise from that set --
// every reachable value of "local" after quadrant reduction is a multiple
// of 2, 3, 4, or 6, and none of those ever equal 1 or 5.
static void base_angle_sin_cos(int64_t local, number *sin0, number *cos0)
{
    switch (local) {
    case 0: *sin0 = small_zero(); *cos0 = number_from_int(1); break;
    case 2: *sin0 = number_from_ratio(1, 2); *cos0 = half_sqrt(3); break;
    case 3: *sin0 = half_sqrt(2); *cos0 = half_sqrt(2); break;
    case 4: *sin0 = half_sqrt(3); *cos0 = number_from_ratio(1, 2); break;
    case 6: *sin0 = number_from_int(1); *cos0 = small_zero(); break;
    default: abort(); // unreachable: see comment above
    }
}

// If x is exactly k*pi for a rational k whose reduced denominator is 1, 2,
// 3, 4, or 6, sin/cos are exact -- the standard special angles (0, pi/6,
// pi/4, pi/3, pi/2, and every reflection/rotation of those around the
// circle: 2pi/3, 3pi/4, pi, 3pi/2, ...). The general engine alone could
// only ever approximate these arbitrarily close to their true value, never
// prove them exactly (e.g. sin(pi) would print as "0.000...0" instead of
// the exact 0 it is).
static bool exact_trig_of_pi_multiple(number x, bool want_sin, number *out)
{
    if (!is_real_kind(x)) return false;
    num_real *r = as_real(x);
    if (r->fn != FN_PI || !number_is_zero(r->a)) return false;
    mpq_view vk;
    mpq_srcptr qk = number_mpq_view(r->b, &vk); // x == k*pi
    mpz_srcptr kden = mpq_denref(qk);
    uint32_t den_val;
    if (bi_is_one(kden)) {
        den_val = 1;
    } else if (mpz_cmp_ui(kden, 2) == 0 || mpz_cmp_ui(kden, 3) == 0 ||
               mpz_cmp_ui(kden, 4) == 0 || mpz_cmp_ui(kden, 6) == 0) {
        den_val = (uint32_t)mpz_get_ui(kden); // one of 2/3/4/6 (guarded above)
    } else {
        return false;
    }
    uint64_t num_mag;
    // Bounded well under INT64_MAX/12 so sign*num_mag*(12/den_val) can't
    // overflow below -- this is a fast path for ordinary angles, not a
    // general bignum-multiple-of-pi solver. bi_fits_u64 reads magnitude
    // limbs, so it works on the signed numerator directly.
    if (!bi_fits_u64(mpq_numref(qk), &num_mag) || num_mag > (uint64_t)INT64_MAX / 12)
        return false;
    int sign = mpq_sgn(qk);

    // m = k * 12 (an integer, since den_val divides 12); reduce mod 24 (the
    // period of pi/12) into [0, 24) -- angle and sign together, so negative
    // k is handled by the reduction itself, not a separate sign case.
    int64_t m = sign * (int64_t)num_mag * (12 / (int64_t)den_val);
    int64_t rmod = ((m % 24) + 24) % 24;

    int64_t local;
    int sin_sign, cos_sign;
    if (rmod <= 6) { local = rmod; sin_sign = 1; cos_sign = 1; }
    else if (rmod <= 12) { local = 12 - rmod; sin_sign = 1; cos_sign = -1; }
    else if (rmod <= 18) { local = rmod - 12; sin_sign = -1; cos_sign = -1; }
    else { local = 24 - rmod; sin_sign = -1; cos_sign = 1; }

    number sin0, cos0;
    base_angle_sin_cos(local, &sin0, &cos0);
    if (want_sin) {
        if (sin_sign < 0) {
            *out = number_neg(sin0);
        } else {
            *out = sin0;
        }
    } else {
        if (cos_sign < 0) {
            *out = number_neg(cos0);
        } else {
            *out = cos0;
        }
    }
    return true;
}

// sin(x): 0 -> exact 0; k*pi (k rational with denominator 1 or 2) -> exact;
// otherwise the general engine (range-reduced mod 2pi).
number number_sin(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return small_zero();
    number exact;
    if (exact_trig_of_pi_multiple(x, true, &exact)) return exact;
    return make_irr(IRR_SIN, x, small_zero());
}

// cos(x): 0 -> exact 1; k*pi (as above) -> exact; otherwise the general engine.
number number_cos(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return number_from_int(1);
    number exact;
    if (exact_trig_of_pi_multiple(x, false, &exact)) return exact;
    return make_irr(IRR_COS, x, small_zero());
}

// tan(x) = sin(x)/cos(x). Poles (x == pi/2 + k*pi) surface as NUMBER_ERROR
// via division's inability to prove cos(x) nonzero, not a domain check here.
number number_tan(number x)
{
    if (number_is_error(x)) return x;
    number s = number_sin(x);
    number c = number_cos(x);
    number result = number_div(s, c);
    return result;
}

// Shared tail for exact_atan_of_special_value and exact_asin_of_special_value:
// given the pi/12 multiple already identified (twelfths < 0 means "no exact
// value"), applies x's sign and returns twelfths/12 * pi via out.
static bool twelfths_times_pi(number x, int64_t twelfths, number *out)
{
    if (twelfths < 0) return false;
    if (number_is_negative(x)) twelfths = -twelfths;
    number pi = number_pi();
    number frac = number_from_ratio(twelfths, 12);
    *out = number_mul(frac, pi);
    return true;
}

// If |x| equals one of the standard tan values (sqrt(3)/3, 1, sqrt(3)),
// atan(x) is an exact multiple of pi/12 (pi/6, pi/4, pi/3) -- the inverse
// direction of exact_trig_of_pi_multiple. atan(0) is handled by
// number_atan's own escape before this is ever called.
static bool exact_atan_of_special_value(number x, number *out)
{
    number ax = number_abs(x);
    number one = number_from_int(1);
    number sqrt3 = number_sqrt(number_from_int(3));
    number sqrt3_3 = number_div(sqrt3, number_from_int(3));

    int64_t twelfths = -1;
    if (number_equal(ax, sqrt3_3)) twelfths = 2;
    else if (number_equal(ax, one)) twelfths = 3;
    else if (number_equal(ax, sqrt3)) twelfths = 4;
    return twelfths_times_pi(x, twelfths, out);
}

// atan(x): 0 -> exact 0; +-sqrt(3)/3, +-1, +-sqrt(3) -> exact (+-pi/6, pi/4,
// pi/3); otherwise the general engine. Defined for all x.
number number_atan(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return small_zero();
    number exact;
    if (exact_atan_of_special_value(x, &exact)) return exact;
    return make_irr(IRR_ATAN, x, small_zero());
}

// atan2(y, x): the angle in (-pi, pi] of the point (x, y), i.e. atan(y/x)
// placed in the correct quadrant by x's and y's signs. On the y-axis (x == 0)
// the result is exactly +-pi/2; number_atan already yields exact multiples of
// pi/12 where applicable, so e.g. atan2(1, 1) == pi/4 and atan2(1, -1) ==
// 3*pi/4 exactly. atan2(0, 0) has no defined direction -> error (this differs
// from C's atan2, which returns 0). An error operand propagates.
number number_atan2(number y, number x)
{
    if (number_is_error(y)) return y;
    if (number_is_error(x)) return x;
    int sx = number_sign(x), sy = number_sign(y);
    if (sx == 0 && sy == 0) return err(ERR_ATAN2_ORIGIN);
    if (sx == 0) { // straight up (+pi/2) or down (-pi/2)
        number pi = number_pi();
        number half = number_from_ratio(sy > 0 ? 1 : -1, 2);
        number res = number_mul(pi, half);
        return res;
    }
    number q = number_div(y, x);
    number at = number_atan(q); // principal value in (-pi/2, pi/2)
    if (sx > 0) return at; // right half-plane: atan(y/x) is already correct
    // Left half-plane: shift by +pi for y >= 0, -pi for y < 0 (so the branch
    // cut sits on the negative x-axis, which maps to +pi).
    number pi = number_pi();
    number res = sy >= 0 ? number_add(at, pi) : number_sub(at, pi);
    return res;
}

// If |x| equals one of the standard sin values (1/2, sqrt(2)/2, sqrt(3)/2),
// asin(x) is an exact multiple of pi/12 (pi/6, pi/4, pi/3) -- the inverse
// direction of exact_trig_of_pi_multiple. asin(0) and asin(+-1) are handled
// by number_asin's own escapes before this is ever called.
static bool exact_asin_of_special_value(number x, number *out)
{
    number ax = number_abs(x);
    number half = number_from_ratio(1, 2);
    number sqrt2_2 = half_sqrt(2);
    number sqrt3_2 = half_sqrt(3);

    int64_t twelfths = -1;
    if (number_equal(ax, half)) twelfths = 2;
    else if (number_equal(ax, sqrt2_2)) twelfths = 3;
    else if (number_equal(ax, sqrt3_2)) twelfths = 4;
    return twelfths_times_pi(x, twelfths, out);
}

// asin(x) = atan(x/sqrt(1-x^2)), domain |x| <= 1. Exact at x == 0, +-1/2,
// +-sqrt(2)/2, +-sqrt(3)/2, +-1 (0, +-pi/6, pi/4, pi/3, pi/2). 1-x^2 goes
// through number_sqrt, so this works for an already-irrational x too (e.g.
// asin(sin(0.5))): number_sqrt falls back to the general engine rather
// than erroring, and so does this.
number number_asin(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return small_zero();
    number one = number_from_int(1);
    number neg_one = number_neg(one);
    int cmp_hi = number_compare(x, one);
    int cmp_lo = number_compare(x, neg_one);
    if (cmp_hi > 0 || cmp_lo < 0) {
        return err(ERR_ASIN_ACOS_DOMAIN); // |x| > 1: outside asin's domain
    }
    if (cmp_hi == 0 || cmp_lo == 0) {
        number pi = number_pi();
        number two = number_from_int(2);
        number half_pi = number_div(pi, two);
        if (cmp_lo == 0) {
            number result = number_neg(half_pi);
            return result;
        }
        return half_pi;
    }
    number exact;
    if (exact_asin_of_special_value(x, &exact)) {
        return exact;
    }
    number x2 = number_mul(x, x);
    number one_minus_x2 = number_sub(one, x2);
    number denom = number_sqrt(one_minus_x2); // 1-x^2 > 0 here (|x| < 1 strictly)
    number ratio = number_div(x, denom);
    number result = number_atan(ratio);
    return result;
}

// acos(x) = pi/2 - asin(x). Same domain as asin.
number number_acos(number x)
{
    if (number_is_error(x)) return x;
    number asinx = number_asin(x);
    if (number_is_error(asinx)) return asinx;
    number pi = number_pi();
    number two = number_from_int(2);
    number half_pi = number_div(pi, two);
    number result = number_sub(half_pi, asinx);
    return result;
}

// sinh/cosh/tanh via exp: no new IRR ops, pure composition. cosh(x) is
// never zero for real x, so tanh has no pole to worry about (unlike tan).
number number_sinh(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return small_zero();
    number ex = number_exp(x);
    number negx = number_neg(x);
    number enx = number_exp(negx);
    number diff = number_sub(ex, enx);
    number two = number_from_int(2);
    number result = number_div(diff, two);
    return result;
}

number number_cosh(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return number_from_int(1);
    number ex = number_exp(x);
    number negx = number_neg(x);
    number enx = number_exp(negx);
    number sum = number_add(ex, enx);
    number two = number_from_int(2);
    number result = number_div(sum, two);
    return result;
}

number number_tanh(number x)
{
    if (number_is_error(x)) return x;
    if (number_is_zero(x)) return small_zero();
    number s = number_sinh(x);
    number c = number_cosh(x);
    number result = number_div(s, c);
    return result;
}

// x^n for integer n (any sign), via exact repeated squaring. x != 0.
static number int_pow(number x, int64_t n)
{
    bool neg = n < 0;
    uint64_t un = neg ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n; // avoid negating INT64_MIN
    number base = x;
    number result = number_from_int(1);
    while (un) {
        if (un & 1) {
            number next = number_mul(result, base);
            result = next;
        }
        un >>= 1;
        if (un) {
            number nb = number_mul(base, base);
            base = nb;
        }
    }
    if (neg) {
        number inv = number_inverse(result);
        return inv;
    }
    return result;
}

// x^(1/q) exactly, if rational x (already checked >= 0 by the caller for
// even q) has a rational q-th root -- i.e. its numerator and denominator
// are each a perfect q-th power. Only ever exact or "doesn't apply": there
// is no symbolic cbrt(n)-style form for q > 2 the way there is for
// sqrt(n), so a non-exact root just returns false (the general engine
// still gets the numerically correct answer, just not an exact one).
static bool exact_rational_root(number x, uint32_t q, number *out)
{
    mpq_view v;
    mpq_srcptr xq = number_mpq_view(x, &v);
    mpq_t r;
    mpq_init(r);
    // mpz_root is signed: an odd q keeps a negative numerator's sign, and
    // the caller has already excluded negative x for even q.
    bool exact_n = mpz_root(mpq_numref(r), mpq_numref(xq), q) != 0;
    bool exact_d = mpz_root(mpq_denref(r), mpq_denref(xq), q) != 0;
    if (!exact_n || !exact_d) {
        mpq_clear(r);
        return false;
    }
    *out = number_from_mpq(r); // roots of a coprime pair are coprime: canonical
    return true;
}

// x^y: a rational-escape ladder before falling back to exp(y*ln(x)).
number number_pow(number x, number y)
{
    if (number_is_error(x)) return x;
    if (number_is_error(y)) return y;
    if (number_is_zero(y)) return number_from_int(1); // x^0 == 1, including 0^0
    if (number_is_zero(x)) return number_sign(y) > 0 ? small_zero() : err(ERR_POW_ZERO_NEGATIVE);

    number one = number_from_int(1);
    bool y_is_one = number_equal(y, one);
    if (y_is_one) return x;

    if (number_is_rational(y)) {
        bool y_int;
        int64_t yi = number_to_int64(y, &y_int);
        if (y_int && yi != INT64_MIN) // int_pow negates: exclude INT64_MIN
            return int_pow(x, yi);
        mpq_view vy;
        mpq_srcptr qy = number_mpq_view(y, &vy);

        // Non-integer rational exponent p/q (q > 1, since is_int is false):
        // q == 2 keeps the existing sqrt path (which also covers the
        // *irrational* sqrt(n) symbolic form for non-perfect-squares); q > 2
        // only has an exact result when x itself is a rational q-th power
        // (no symbolic root form exists for q > 2), otherwise falls through
        // to the general engine below.
        uint64_t qmag = 0, pmag = 0;
        bool q_fits = bi_fits_u64(mpq_denref(qy), &qmag) && qmag <= 1000; // a "nice" root has a small q
        bool p_fits = bi_fits_u64(mpq_numref(qy), &pmag) && pmag <= (uint64_t)INT64_MAX;
        int psign = mpq_sgn(qy);
        int64_t p = psign < 0 ? -(int64_t)pmag : (int64_t)pmag;

        if (q_fits && qmag == 2 && p_fits) {
            if (number_sign(x) < 0) return err(ERR_SQRT_NEGATIVE);
            number root = number_sqrt(x);
            number result = int_pow(root, p);
            return result;
        }
        if (q_fits && qmag > 2 && p_fits && number_is_rational(x)) {
            bool even_root = qmag % 2 == 0;
            if (!(even_root && number_is_negative(x))) {
                number root;
                if (exact_rational_root(x, (uint32_t)qmag, &root)) {
                    number result = int_pow(root, p);
                    return result;
                }
            }
        }
    }

    if (number_sign(x) < 0) return err(ERR_POW_NEGATIVE_BASE); // complex result
    number lnx = number_ln(x);
    number ylnx = number_mul(y, lnx);
    number result = number_exp(ylnx);
    return result;
}

// If sqrt(r2) is a rational multiple of sqrt(r1), set *conv to that multiple.
static bool sqrt_commensurable(number r1, number r2, number *conv)
{
    if (number_equal(r1, r2)) {
        *conv = number_from_int(1);
        return true;
    }
    number m = number_mul(r1, r2);
    number s;
    bool ok = rational_sqrt_exact(m, &s);
    if (!ok) return false;
    *conv = number_div(s, r1); // sqrt(r2) = (sqrt(r1*r2)/r1) * sqrt(r1)
    return true;
}

// If b1 and b2 are both integers (any sign), sets *out to r1^b1 * r2^b2 (a
// positive rational) and returns true: b1*ln(r1) + b2*ln(r2) then collapses
// to ln(*out) with coefficient 1, e.g. 2*ln(2) - ln(4) combines to ln(1) =
// 0 instead of two unrelated LN terms. False (leaving the two terms
// unrepresentable as one, so the caller falls back to the general engine)
// when either coefficient is non-integer, e.g. ln(2) + (1/2)*ln(3).
static bool ln_combine(number b1, number r1, number b2, number r2, number *out)
{
    bool ok1, ok2;
    int64_t i1 = number_to_int64(b1, &ok1), i2 = number_to_int64(b2, &ok2);
    if (!ok1 || !ok2 || i1 == INT64_MIN || i2 == INT64_MIN) // int_pow negates
        return false;
    number p1 = int_pow(r1, i1);
    number p2 = int_pow(r2, i2);
    *out = number_mul(p1, p2);
    return true;
}

// At least one operand is a real; neither is an error.
static number real_add(number x, number y)
{
    if (!is_real_kind(x)) {
        number t = x;
        x = y;
        y = t;
    }
    num_real *rx = as_real(x);
    if (!is_real_kind(y)) { // real + rational: shift the rational part
        return make_real(number_add(rx->a, y), rx->b,
                         rx->fn, rx->arg);
    }
    num_real *ry = as_real(y);
    if (rx->fn != ry->fn)
        return NUMBER_ERROR; // pi + sqrt(n): outside every representable form
    if (rx->fn == FN_PI) {
        return make_real(number_add(rx->a, ry->a), number_add(rx->b, ry->b),
                         FN_PI, small_zero());
    }
    if (rx->fn == FN_EXP) {
        if (!number_equal(rx->arg, ry->arg))
            return NUMBER_ERROR; // exp(a) + exp(b), a != b: not representable
        return make_real(number_add(rx->a, ry->a), number_add(rx->b, ry->b),
                         FN_EXP, rx->arg);
    }
    if (rx->fn == FN_LN) {
        if (number_equal(rx->arg, ry->arg)) {
            return make_real(number_add(rx->a, ry->a), number_add(rx->b, ry->b),
                             FN_LN, rx->arg);
        }
        // ln(r1)*b1 + ln(r2)*b2 = ln(r1^b1 * r2^b2) when b1, b2 are both
        // integers (see ln_combine): folds to a single LN term.
        number combined;
        if (!ln_combine(rx->b, rx->arg, ry->b, ry->arg, &combined))
            return NUMBER_ERROR; // e.g. ln(2) + (1/2)*ln(3): not representable
        return make_real(number_add(rx->a, ry->a), number_from_int(1),
                         FN_LN, combined);
    }
    number conv;
    if (!sqrt_commensurable(rx->arg, ry->arg, &conv))
        return NUMBER_ERROR; // e.g. sqrt(2) + sqrt(3)
    number b2 = number_mul(ry->b, conv);
    number a = number_add(rx->a, ry->a);
    number b = number_add(rx->b, b2);
    return make_real(a, b, FN_SQRT, rx->arg);
}

static number real_mul(number x, number y)
{
    if (!is_real_kind(x)) {
        number t = x;
        x = y;
        y = t;
    }
    num_real *rx = as_real(x);
    if (!is_real_kind(y)) { // scale by a rational
        if (number_is_zero(y)) return small_zero();
        return make_real(number_mul(rx->a, y), number_mul(rx->b, y),
                         rx->fn, rx->arg);
    }
    num_real *ry = as_real(y);
    if (rx->fn == FN_EXP && ry->fn == FN_EXP &&
        number_is_zero(rx->a) && number_is_zero(ry->a)) {
        // b1*exp(e1) * b2*exp(e2) = b1*b2*exp(e1+e2); only the pure (no
        // rational addend) form multiplies out this way -- (a1+b1 e^x1)
        // (a2+b2 e^x2) with a1 or a2 nonzero expands into terms this
        // linear a+b*F form can't hold.
        number sum = number_add(rx->arg, ry->arg);
        number coeff = number_mul(rx->b, ry->b);
        return make_real(small_zero(), coeff, FN_EXP, sum);
    }
    if (rx->fn != FN_SQRT || ry->fn != FN_SQRT)
        return NUMBER_ERROR; // pi*pi, pi*sqrt(n), exp(x)*exp(y) [mixed a!=0]: not representable
    number conv;
    if (sqrt_commensurable(rx->arg, ry->arg, &conv)) {
        // Rewrite y over sqrt(r) with r = rx->arg, then multiply in
        // Q(sqrt r): (a1 + b1*s)(a2 + b2*s) = a1a2 + b1b2*r + (a1b2 + a2b1)*s
        number b2 = number_mul(ry->b, conv);
        number a1a2 = number_mul(rx->a, ry->a);
        number b1b2 = number_mul(rx->b, b2);
        number b1b2r = number_mul(b1b2, rx->arg);
        number a = number_add(a1a2, b1b2r);
        number t1 = number_mul(rx->a, b2);
        number t2 = number_mul(ry->a, rx->b);
        number b = number_add(t1, t2);
        return make_real(a, b, FN_SQRT, rx->arg);
    }
    if (number_is_zero(rx->a) && number_is_zero(ry->a)) {
        // b1*sqrt(r1) * b2*sqrt(r2) = b1*b2*sqrt(r1*r2)
        number m = number_mul(rx->arg, ry->arg);
        number root = number_sqrt(m);
        number coeff = number_mul(rx->b, ry->b);
        number res = number_mul(root, coeff);
        return res;
    }
    return NUMBER_ERROR; // (a1+b1*sqrt 2)(a2+b2*sqrt 3): three distinct radicals
}

// y is nonzero and at least one operand is a real; neither is an error.
static number real_div(number x, number y)
{
    if (!is_real_kind(y)) { // real / rational
        number inv = number_inverse(y);
        number res = real_mul(x, inv);
        return res;
    }
    num_real *ry = as_real(y);
    if (ry->fn == FN_EXP && number_is_zero(ry->a)) {
        // 1/(b*exp(e)) = (1/b)*exp(-e): only the pure (no rational addend)
        // form inverts inside this closed set -- 1/(a+b*exp(e)) with a != 0
        // isn't again of the a+b*F shape.
        number inv_b = number_inverse(ry->b);
        number neg_arg = number_neg(ry->arg);
        number inv = make_real(small_zero(), inv_b, FN_EXP, neg_arg);
        number res = number_mul(x, inv);
        return res;
    }
    if (ry->fn != FN_SQRT)
        return NUMBER_ERROR; // 1/(a+b*pi), 1/(a+b*ln r), 1/(a+b*exp e) [a!=0]: outside every form
    // 1/(a + b*sqrt r) = (a - b*sqrt r) / (a^2 - b^2*r); the denominator is
    // nonzero because sqrt(r) is irrational.
    number a2 = number_mul(ry->a, ry->a);
    number b2 = number_mul(ry->b, ry->b);
    number b2r = number_mul(b2, ry->arg);
    number norm = number_sub(a2, b2r);
    number ca = number_div(ry->a, norm);
    number cb_pos = number_div(ry->b, norm);
    number cb = number_neg(cb_pos);
    number inv = make_real(ca, cb, FN_SQRT, ry->arg);
    number res = number_mul(x, inv);
    return res;
}

// --- Approximation engine ---
//
// Fixed-point approximations: a (sign, magnitude) pair V approximating
// value * 2^w with |error| <= 4. Callers that need certainty evaluate both
// ends of the +-4 interval and retry at double the precision when the two
// ends disagree; termination is guaranteed whenever the approximated value
// cannot equal the decision boundary (always true for irrationals).

// Moves z (signed) out into a fresh heap bigint magnitude, returning its
// sign; z is cleared. The bridge from a series loop's in-place signed mpz
// locals back to the (sign, magnitude-bigint) protocol the engine speaks.
static int mpz_take(mpz_t z, bigint **out)
{
    int s = mpz_sgn(z);
    mpz_abs(z, z);
    bigint *b = bi_new();
    mpz_swap(b, z);
    mpz_clear(z);
    *out = b;
    return s;
}

// The series loops below run on in-place mpz_t locals: each iteration
// reuses the same term/sum/power buffers (GMP grows them once and then
// they're steady-state), where the old bigint value style allocated and
// freed several temporaries per term. Signs ride in the mpz's themselves --
// mpz division truncates toward zero, which matches the old
// magnitude-truncate-then-apply-sign arithmetic exactly.

// arctan(1/x) * 2^w by truncated Taylor series; x*x must fit in uint32.
static bigint *arctan_recip(uint32_t x, uint32_t w)
{
    mpz_t p, term, sum;
    mpz_init_set_ui(p, 1);
    mpz_mul_2exp(p, p, w);
    mpz_tdiv_q_ui(p, p, x); // x^-(2k+1) * 2^w, k = 0
    mpz_init(term);
    mpz_init(sum);
    for (uint32_t k = 0; mpz_sgn(p) != 0; k++) {
        mpz_tdiv_q_ui(term, p, 2 * k + 1);
        if (k % 2 == 0) mpz_add(sum, sum, term);
        else mpz_sub(sum, sum, term);
        mpz_tdiv_q_ui(p, p, x * x);
    }
    mpz_clear(p);
    mpz_clear(term);
    bigint *res;
    mpz_take(sum, &res); // sum > 0: the k=0 term dominates
    return res;
}

// pi * 2^prec with |error| <= 2, via Machin's formula:
// pi = 16*arctan(1/5) - 4*arctan(1/239). The 24 guard bits absorb the
// per-term truncation slop (~5 * work-bits) for any precision below ~10^6.
static bigint *machin_pi(uint32_t prec)
{
    uint32_t w = prec + 24;
    bigint *a5 = arctan_recip(5, w);
    bigint *a239 = arctan_recip(239, w);
    bigint *t16 = bi_shl(a5, 4);
    bigint *t4 = bi_shl(a239, 2);
    bigint *diff = bi_sub(t16, t4);
    bi_free(a5);
    bi_free(a239);
    bi_free(t16);
    bi_free(t4);
    bigint *res = bi_shr(diff, 24);
    bi_free(diff);
    return res;
}

static bigint *g_pi_appr = NULL; // global memo for the shared constant pi
static uint32_t g_pi_prec = 0;

// pi * 2^prec with |error| <= 2, memoized globally. Standalone (not just
// factor_appr's FN_PI case) so irr_fixed's SIN/COS range reduction can reach
// it without a num_real to hang off of.
static bigint *pi_appr(uint32_t prec)
{
    if (g_pi_appr == NULL || g_pi_prec < prec) {
        uint32_t p = prec + 64; // overshoot so nearby requests hit the memo
        bigint *fresh = machin_pi(p);
        bi_free(g_pi_appr);
        g_pi_appr = fresh;
        g_pi_prec = p;
    }
    return bi_shr(g_pi_appr, g_pi_prec - prec);
}

// F * 2^prec with |error| <= 2, memoized (globally for pi, per-object for
// sqrt/ln/exp). Returns a fresh bigint owned by the caller. The 64-bit
// overshoot every branch uses before storing into r->appr makes whatever
// error the underlying computation has (<=1 for isqrt, <=2 for pi_appr,
// <=4 for exp_fixed_core/ln_fixed_core) utterly negligible once shifted
// back down by 64 bits to the requested prec -- so the <=2 contract holds
// regardless of which branch produced it.
static bigint *factor_appr(num_real *r, uint32_t prec)
{
    if (r->fn == FN_PI) return pi_appr(prec);
    if (r->appr_prec == 0 || r->appr_prec < prec) {
        uint32_t p = prec + 64; // overshoot so nearby requests hit the memo
        bigint *fresh;
        if (r->fn == FN_LN) {
            // r->arg > 1 (make_real's canonicalization), so ln(arg) > 0:
            // always succeeds here, unlike the general IRR_LN case.
            int sign;
            if (!ln_fixed_core(r->arg, p, &sign, &fresh)) abort();
        } else if (r->fn == FN_EXP) {
            int sign;
            if (!exp_fixed_core(r->arg, p, &sign, &fresh)) abort();
        } else { // FN_SQRT of a positive non-square integer
            mpq_view vr;
            mpq_srcptr rq = number_mpq_view(r->arg, &vr);
            mpz_t shifted;
            mpz_init(shifted);
            mpz_mul_2exp(shifted, mpq_numref(rq), 2 * p);
            fresh = bi_new();
            mpz_sqrt(fresh, shifted); // floor(sqrt(n) * 2^p): error <= 1
            mpz_clear(shifted);
        }
        mpz_swap(r->appr, fresh);
        bi_free(fresh); // now holds the previous memo
        r->appr_prec = p;
    }
    return bi_shr(r->appr, r->appr_prec - prec);
}

// (s1, m1) + (s2, m2) -> (sign, *out); consumes m1 and m2.
static int signed_add(int s1, bigint *m1, int s2, bigint *m2, bigint **out)
{
    int sign;
    if (s1 == s2) {
        *out = bi_add(m1, m2);
        sign = s1;
    } else {
        int c = bi_cmp(m1, m2);
        if (c >= 0) {
            *out = bi_sub(m1, m2);
            sign = s1;
        } else {
            *out = bi_sub(m2, m1);
            sign = s2;
        }
        if (bi_is_zero(*out)) sign = 0;
    }
    bi_free(m1);
    bi_free(m2);
    return sign;
}

// (a + b*F) * 2^w as sign/magnitude with |error| <= 4. a and b are passed
// explicitly so digit generation can fold exact powers of ten into them;
// r supplies F (fn/arg) and its memo.
static void real_fixed(num_real *r, number a, number b, uint32_t w,
                       int *sign_out, bigint **mag_out)
{
    // T = b * F * 2^w, error <= 2: evaluate F with enough extra precision
    // to absorb both F's error scaled by |b| and the final truncation. F is
    // positive (every factor's invariant), so T's sign is b's own, and
    // truncation toward zero matches the old magnitude arithmetic.
    mpq_view vb, va;
    mpq_srcptr qb = number_mpq_view(b, &vb);
    uint32_t hb = (uint32_t)mpz_sizeinbase(mpq_numref(qb), 2); // b != 0
    bigint *F = factor_appr(r, w + hb + 4);
    mpz_t acc, t;
    mpz_init(acc);
    mpz_init(t);
    mpz_mul(acc, mpq_numref(qb), F);
    bi_free(F);
    mpz_mul_2exp(t, mpq_denref(qb), hb + 4);
    mpz_tdiv_q(acc, acc, t);

    // + a * 2^w, error <= 1, truncated toward zero independently (same
    // rounding the old separate-magnitudes signed_add produced)
    mpq_srcptr qa = number_mpq_view(a, &va);
    mpz_mul_2exp(t, mpq_numref(qa), w);
    mpz_tdiv_q(t, t, mpq_denref(qa));
    mpz_add(acc, acc, t);
    mpz_clear(t);
    *sign_out = mpz_take(acc, mag_out);
}

// Any non-error number as fixed-point at precision w, |error| <= 4. False
// if the approximation can't be resolved (see IRR_MAX_PREC).
static bool value_fixed(number x, uint32_t w, int *sign_out, bigint **mag_out)
{
    if (is_real_kind(x)) {
        num_real *r = as_real(x);
        real_fixed(r, r->a, r->b, w, sign_out, mag_out);
        return true;
    }
    if (is_irrational_kind(x))
        return irr_fixed(as_irr(x), w, sign_out, mag_out);
    mpq_view v;
    mpq_srcptr q = number_mpq_view(x, &v);
    mpz_t t;
    mpz_init(t);
    mpz_mul_2exp(t, mpq_numref(q), w);
    mpz_tdiv_q(t, t, mpq_denref(q));
    *sign_out = mpz_take(t, mag_out);
    return true;
}

// ---------------------------------------------------------------------------
// Hardware double-interval fast path (number-design.md "Hardware floating-point
// fast path"). Before any scaled-bigint evaluation, a value can be bounded
// by a hardware-double interval [lo, hi] computed with outward rounding: a
// recursive descent over the same rational/real/DAG structure value_fixed
// walks, each step widening its result by enough ulps to make the bounds
// rigorous. If the interval is tight enough to decide the question being
// asked -- a sign (refine_sign) or a k-digit decimal rounding
// (real_to_string / irr_to_string_digits); number_to_double is
// deliberately excluded, see refine_to_double -- the answer is served at
// hardware speed and is
// still exact; if not (cancellation, overflow, genuinely more precision
// demanded), evaluation falls through to the bigint path unchanged, so a
// wide interval costs one cheap failed attempt, never a wrong answer.
//
// Rigor budget per operation:
//   - IEEE + - * / and sqrt are correctly rounded: 1-ulp outward widening
//     makes their interval extensions rigorous outright.
//   - libm's exp/ln/atan/sin/cos are NOT guaranteed correctly rounded.
//     glibc documents worst-case errors of 1-2 ulp for these functions on
//     every mainstream target (see its "Known Maximum Errors" manual
//     section); DIVAL_LIBM_ULPS widens by 8, a 4x margin over that. A libm
//     exceeding 8 ulps of error would erode this margin silently -- build
//     with -DNUMBER_NO_DOUBLE_FILTER to remove the fast path entirely (and
//     with it any reliance on libm quality) at some cost to
//     approximation-heavy workloads; every result then comes from the
//     bigint path alone.
//   - sin/cos additionally bound by Lipschitz continuity (|sin'| <= 1)
//     around the interval midpoint rather than by monotonicity analysis,
//     so they're valid for any argument width; a huge argument just yields
//     a uselessly wide (but still correct) interval that falls through.
//
// The descent is unmemoized (doubles are too cheap to be worth caching), so
// a heavily-shared DAG could be visited exponentially often; a fixed visit
// budget cuts the attempt off long before that costs anything real and
// falls back to the (memoized) bigint path.

#ifndef NUMBER_NO_DOUBLE_FILTER
typedef struct {
    double lo, hi;
} dival;

static inline double ival_prev(double v) { return nextafter(v, -(double)INFINITY); }
static inline double ival_next(double v) { return nextafter(v, (double)INFINITY); }

#define DIVAL_LIBM_ULPS 8

static double ival_down_k(double v, int k)
{
    for (int i = 0; i < k; i++) v = ival_prev(v);
    return v;
}

static double ival_up_k(double v, int k)
{
    for (int i = 0; i < k; i++) v = ival_next(v);
    return v;
}

static dival dival_add(dival a, dival b)
{
    return (dival){ival_prev(a.lo + b.lo), ival_next(a.hi + b.hi)};
}

static dival dival_sub(dival a, dival b)
{
    return (dival){ival_prev(a.lo - b.hi), ival_next(a.hi - b.lo)};
}

static dival dival_mul(dival a, dival b)
{
    double p1 = a.lo * b.lo, p2 = a.lo * b.hi, p3 = a.hi * b.lo, p4 = a.hi * b.hi;
    return (dival){ival_prev(fmin(fmin(p1, p2), fmin(p3, p4))),
                   ival_next(fmax(fmax(p1, p2), fmax(p3, p4)))};
}

// The correctly rounded double nearest pi; the true pi is strictly inside
// [ival_prev, ival_next] of it. (Spelled in hex rather than M_PI: M_PI is
// a POSIX extension, not ISO C, and this file builds with -Wpedantic.)
#define PI_DOUBLE 0x1.921fb54442d18p+1

// Every dival_of call site's descent shares one visit budget: the DAG is
// walked without memoization, so sharing could otherwise blow up the visit
// count exponentially (e.g. pow's repeated squaring reuses each node
// twice per level). Generous for any expression a fast path should serve;
// a DAG that exhausts it belongs on the memoized bigint path anyway.
#define DIVAL_VISIT_BUDGET 512

static bool dival_of_real(const num_real *r, number a, number b, dival *out, int *budget);

// Rigorous double bounds on any non-error number: *out straddles the true
// value on success. False when bounds can't be established (overflow to
// infinity, a domain edge, or the visit budget running out) -- never
// wrong, just unavailable.
static bool dival_of(number x, dival *out, int *budget)
{
    if (--*budget < 0) return false;
    if (number_is_error(x)) return false;
    if (number_is_rational(x)) {
        double v = number_to_double(x); // correctly rounded for both rational tiers
        if (!isfinite(v)) return false;
        *out = (dival){ival_prev(v), ival_next(v)};
        return true;
    }
    if (is_real_kind(x)) {
        num_real *r = as_real(x);
        return dival_of_real(r, r->a, r->b, out, budget);
    }
    num_irr *n = as_irr(x);
    dival a, res;
    if (!dival_of(n->x, &a, budget)) return false;
    switch (n->op) {
    case IRR_ADD:
    case IRR_SUB:
    case IRR_MUL:
    case IRR_DIV: {
        dival b;
        if (!dival_of(n->y, &b, budget)) return false;
        if (n->op == IRR_ADD) res = dival_add(a, b);
        else if (n->op == IRR_SUB) res = dival_sub(a, b);
        else if (n->op == IRR_MUL) res = dival_mul(a, b);
        else {
            if (b.lo <= 0 && b.hi >= 0) return false; // can't bound across a pole
            double q1 = a.lo / b.lo, q2 = a.lo / b.hi, q3 = a.hi / b.lo, q4 = a.hi / b.hi;
            res = (dival){ival_prev(fmin(fmin(q1, q2), fmin(q3, q4))),
                          ival_next(fmax(fmax(q1, q2), fmax(q3, q4)))};
        }
        break;
    }
    case IRR_EXP:
        res = (dival){ival_down_k(exp(a.lo), DIVAL_LIBM_ULPS),
                      ival_up_k(exp(a.hi), DIVAL_LIBM_ULPS)};
        if (res.lo < 0) res.lo = 0; // exp > 0; widening below the axis helps nobody
        break;
    case IRR_LN:
        if (a.lo <= 0) return false; // interval dips into ln's domain edge
        res = (dival){ival_down_k(log(a.lo), DIVAL_LIBM_ULPS),
                      ival_up_k(log(a.hi), DIVAL_LIBM_ULPS)};
        break;
    case IRR_ATAN:
        res = (dival){ival_down_k(atan(a.lo), DIVAL_LIBM_ULPS),
                      ival_up_k(atan(a.hi), DIVAL_LIBM_ULPS)};
        break;
    case IRR_SQRT:
        if (a.lo < 0) a.lo = 0; // x >= 0 was proven at construction; the interval may still dip
        if (a.hi < 0) return false;
        res = (dival){ival_prev(sqrt(a.lo)), ival_next(sqrt(a.hi))}; // IEEE sqrt: correctly rounded
        break;
    case IRR_SIN:
    case IRR_COS: {
        // Lipschitz bound around the midpoint: |sin(v) - sin(m)| <= |v - m|
        // (same for cos), so [f(m) - h, f(m) + h] with h an upper bound on
        // the half-width is rigorous with no monotonicity analysis, for
        // arguments of any size.
        double m = a.lo + (a.hi - a.lo) / 2;
        if (!isfinite(m)) return false;
        double v = n->op == IRR_SIN ? sin(m) : cos(m);
        double h = fmax(ival_next(m - a.lo), ival_next(a.hi - m));
        res = (dival){ival_down_k(v - h, DIVAL_LIBM_ULPS),
                      ival_up_k(v + h, DIVAL_LIBM_ULPS)};
        if (res.lo < -1) res.lo = -1; // range clamp is always sound for sin/cos
        if (res.hi > 1) res.hi = 1;
        break;
    }
    default:
        return false; // unreachable: every IRR_* op is handled above
    }
    if (!isfinite(res.lo) || !isfinite(res.hi)) return false;
    *out = res;
    return true;
}

// Bounds for the linear form a + b*F. a and b are parameters (not read
// from r) for the same reason real_fixed's are: digit generation folds an
// exact power of ten into them first.
static bool dival_of_real(const num_real *r, number a, number b, dival *out, int *budget)
{
    dival ia, ib, f;
    if (!dival_of(a, &ia, budget) || !dival_of(b, &ib, budget)) return false;
    if (r->fn == FN_PI) {
        f = (dival){ival_prev(PI_DOUBLE), ival_next(PI_DOUBLE)};
    } else if (r->fn == FN_SQRT) {
        double av = number_to_double(r->arg); // correctly rounded
        if (!isfinite(av)) return false;
        f = (dival){ival_prev(sqrt(ival_prev(av))), ival_next(sqrt(ival_next(av)))};
    } else if (r->fn == FN_LN) {
        // r->arg > 1 (make_real's canonicalization), so ln(arg) > 0 -- no
        // domain edge to guard, unlike dival_of's general IRR_LN case.
        double av = number_to_double(r->arg);
        if (!isfinite(av)) return false;
        f = (dival){ival_down_k(log(ival_prev(av)), DIVAL_LIBM_ULPS),
                    ival_up_k(log(ival_next(av)), DIVAL_LIBM_ULPS)};
    } else { // FN_EXP
        double av = number_to_double(r->arg);
        if (!isfinite(av)) return false;
        f = (dival){ival_down_k(exp(ival_prev(av)), DIVAL_LIBM_ULPS),
                    ival_up_k(exp(ival_next(av)), DIVAL_LIBM_ULPS)};
        if (f.lo < 0) f.lo = 0; // exp > 0; widening below the axis helps nobody
    }
    dival res = dival_add(ia, dival_mul(ib, f));
    if (!isfinite(res.lo) || !isfinite(res.hi)) return false;
    *out = res;
    return true;
}

// Decide N = round(v) for the (irrational) value bounded by iv, exactly:
// succeeds only when every value in the interval rounds to the same
// integer, which also rules out ever resolving a tie here (a half-integer
// boundary inside the widened interval always straddles). Matches the
// bigint digit loops' round-half-away-from-zero-on-the-magnitude exactly
// on every decidable case, since the cases where the two rules differ
// (exact ties) are never decidable from a widened interval.
static bool dival_round_int(dival iv, int *sign_out, bigint **mag_out)
{
    double flo = floor(ival_prev(iv.lo + 0.5));
    double fhi = floor(ival_next(iv.hi + 0.5));
    if (flo != fhi) return false;
    if (fabs(flo) >= 9007199254740992.0) return false; // 2^53: past exact-integer doubles
    *sign_out = flo < 0 ? -1 : flo > 0 ? 1 : 0;
    *mag_out = bi_from_u64((uint64_t)fabs(flo));
    return true;
}
#endif // NUMBER_NO_DOUBLE_FILTER

// Sign of a real/irrational value: refine until the interval excludes zero,
// giving up past IRR_MAX_PREC bits. Always succeeds for a real (b != 0
// guarantees it's irrational, hence nonzero); an irrational node might not
// resolve -- see IRR_MAX_PREC's doc comment.
static bool refine_sign(number x, int *sign_out)
{
#ifndef NUMBER_NO_DOUBLE_FILTER
    // Floating-point filter (the computational-geometry pattern): when the
    // hardware interval already excludes zero, the sign is decided in a few
    // nanoseconds and no bigint is ever touched.
    dival iv;
    int budget = DIVAL_VISIT_BUDGET;
    if (dival_of(x, &iv, &budget)) {
        if (iv.lo > 0) {
            NSTAT(interval_hits);
            *sign_out = 1;
            return true;
        }
        if (iv.hi < 0) {
            NSTAT(interval_hits);
            *sign_out = -1;
            return true;
        }
        NSTAT(interval_fallbacks); // straddles zero at double width: refine for real
    }
#endif
    for (uint32_t w = 32; w <= IRR_MAX_PREC; w *= 2) {
        int s;
        bigint *v;
        if (!value_fixed(x, w, &s, &v)) return false;
        bigint *four = bi_from_u64(4);
        bool decided = bi_cmp(v, four) > 0;
        bi_free(four);
        bi_free(v);
        if (decided) {
            *sign_out = s;
            return true;
        }
    }
    return false;
}

// floor(log2(|x|)), approximately (+-1); can be negative for |x| < 1. Used
// to budget extra precision in irr_fixed's MUL/DIV cases: how much a
// *sibling* operand needs (MUL) or how much absolute precision an operand
// itself needs (DIV, when its magnitude is small). Cheap and approximate on
// purpose -- callers pad with guard bits. 0 on failure (a safe, if possibly
// too-small, estimate -- the real evaluation call downstream will itself
// fail and propagate correctly).
static int32_t approx_exponent(number x)
{
    int s;
    bigint *v;
    if (!value_fixed(x, 64, &s, &v)) return 0;
    int32_t bits = (int32_t)bi_bitlen(v); // v approximates |x| * 2^64
    bi_free(v);
    return bits - 64;
}

// exp(sign * |rmag|/2^p) * 2^p via out, returning its sign; |rmag| < 2^p
// (i.e. |r| < 1). Self-terminating Taylor series: exp(r) = sum r^k/k!.
// term carries its sign natively (mpz), so the alternation for negative r
// is just multiplying by the signed r each step.
static int exp_series(int sign, const bigint *rmag, uint32_t p, bigint **out)
{
    mpz_t term, sum;
    mpz_init_set_ui(term, 1);
    mpz_mul_2exp(term, term, p); // term_0 = 1 * 2^p
    mpz_init_set(sum, term);
    for (uint32_t k = 1; mpz_sgn(term) != 0; k++) {
        mpz_mul(term, term, rmag);
        if (sign < 0) mpz_neg(term, term);
        mpz_tdiv_q_2exp(term, term, p);
        mpz_tdiv_q_ui(term, term, k);
        mpz_add(sum, sum, term);
    }
    mpz_clear(term);
    return mpz_take(sum, out);
}

// atanh(|zmag|/2^p) * 2^p, |zmag| < 2^p (|z| < 1), z >= 0. Self-terminating
// series like arctan_recip, but multiplicative (z is a general fixed-point
// fraction here, not a reciprocal integer) and without sign alternation:
// atanh(z) = z + z^3/3 + z^5/5 + ... -- every term is nonnegative, so
// (unlike exp_series) plain bi_add accumulation is fine.
static bigint *atanh_fixed(const bigint *zmag, uint32_t p)
{
    mpz_t z2p, pk, term, sum;
    mpz_init(z2p);
    mpz_mul(z2p, zmag, zmag);
    mpz_tdiv_q_2exp(z2p, z2p, p); // z^2 * 2^p
    mpz_init_set(pk, zmag);       // p_0 = z^1 * 2^p
    mpz_init(term);
    mpz_init(sum);
    for (uint32_t k = 0; mpz_sgn(pk) != 0; k++) {
        mpz_tdiv_q_ui(term, pk, 2 * k + 1);
        mpz_add(sum, sum, term);
        mpz_mul(pk, pk, z2p);
        mpz_tdiv_q_2exp(pk, pk, p); // p_{k+1} = p_k * z^2, still scaled by 2^p
    }
    mpz_clear(z2p);
    mpz_clear(pk);
    mpz_clear(term);
    bigint *res;
    mpz_take(sum, &res); // every term nonnegative
    return res;
}

static bigint *g_ln2_appr = NULL; // global memo for the shared constant ln(2)
static uint32_t g_ln2_prec = 0;

// ln(2) * 2^prec, memoized (same pattern as g_pi_appr). ln(2) = 2*atanh(1/3):
// unlike every other x, ln(2) can't go through the general mantissa/exponent
// reduction below (extracting 2's own exponent needs ln(2) already), so it's
// computed directly here.
static bigint *ln2_appr(uint32_t prec)
{
    if (g_ln2_appr == NULL || g_ln2_prec < prec) {
        uint32_t p = prec + 32;
        bigint *one = bi_from_u64(1);
        bigint *scaled = bi_shl(one, p);
        bi_free(one);
        bigint *third = bi_new();
        mpz_tdiv_q_ui(third, scaled, 3); // (1/3) * 2^p; unsigned long is a full limb here
        bi_free(scaled);
        bigint *at = atanh_fixed(third, p); // atanh(1/3) * 2^p
        bi_free(third);
        bigint *fresh = bi_shl(at, 1); // ln(2) = 2*atanh(1/3)
        bi_free(at);
        bi_free(g_ln2_appr);
        g_ln2_appr = fresh;
        g_ln2_prec = p;
    }
    return bi_shr(g_ln2_appr, g_ln2_prec - prec);
}

// sin(sign*|rmag|/2^p) * 2^p via out, returning its sign; |rmag| <= pi*2^p
// (i.e. |r| <= pi -- NOT reduced further to a tiny range, so this needs more
// terms than exp_series/atanh_fixed for the same precision, but still
// converges: the factorial denominator beats any fixed |r|). Self-
// terminating, alternating: sin(r) = r - r^3/3! + r^5/5! - ...
static int sin_series(int sign, const bigint *rmag, uint32_t p, bigint **out)
{
    mpz_t r2p, term, sum;
    mpz_init(r2p);
    mpz_mul(r2p, rmag, rmag);
    mpz_tdiv_q_2exp(r2p, r2p, p); // r^2 * 2^p
    mpz_init_set(term, rmag);     // term_0 = sign * |r|
    if (sign < 0) mpz_neg(term, term);
    mpz_init_set(sum, term);
    for (uint32_t k = 1; mpz_sgn(term) != 0; k++) {
        mpz_mul(term, term, r2p);
        mpz_neg(term, term); // alternating series
        mpz_tdiv_q_2exp(term, term, p);
        mpz_tdiv_q_ui(term, term, 2 * k);
        mpz_tdiv_q_ui(term, term, 2 * k + 1);
        mpz_add(sum, sum, term);
    }
    mpz_clear(term);
    mpz_clear(r2p);
    return mpz_take(sum, out);
}

// cos(|rmag|/2^p) * 2^p via out, returning its sign; |rmag| <= pi*2^p. cos
// is even, so (unlike sin) the sign of r doesn't matter -- only |r| does.
// Self-terminating, alternating: cos(r) = 1 - r^2/2! + r^4/4! - ...
static int cos_series(const bigint *rmag, uint32_t p, bigint **out)
{
    mpz_t r2p, term, sum;
    mpz_init(r2p);
    mpz_mul(r2p, rmag, rmag);
    mpz_tdiv_q_2exp(r2p, r2p, p); // r^2 * 2^p
    mpz_init_set_ui(term, 1);
    mpz_mul_2exp(term, term, p); // term_0 = 1 * 2^p
    mpz_init_set(sum, term);
    for (uint32_t k = 1; mpz_sgn(term) != 0; k++) {
        mpz_mul(term, term, r2p);
        mpz_neg(term, term); // alternating series
        mpz_tdiv_q_2exp(term, term, p);
        mpz_tdiv_q_ui(term, term, 2 * k - 1);
        mpz_tdiv_q_ui(term, term, 2 * k);
        mpz_add(sum, sum, term);
    }
    mpz_clear(term);
    mpz_clear(r2p);
    return mpz_take(sum, out);
}

// Reduces x into r in (-pi,pi] (sign + magnitude at precision P) via
// x mod 2*pi. Needs pi at boosted precision that scales with x's own
// magnitude, not just the output precision P: a coarse pi lets the quotient
// floor(x/2pi) -- which grows with x's magnitude -- amplify pi's own
// truncation error. This is the classic large-argument range-reduction
// trap (see the caller's guard-bit comment for the actual budget).
static bool reduce_mod_2pi(number x, uint32_t P, int *sign_out, bigint **rmag_out)
{
    int sx;
    bigint *X;
    if (!value_fixed(x, P, &sx, &X)) return false;
    bigint *pi_p = pi_appr(P);
    bigint *two_pi = bi_shl(pi_p, 1);
    bi_free(pi_p);
    bigint *Q, *R;
    bi_divmod(X, two_pi, &Q, &R); // R in [0, two_pi): |x| mod 2pi
    bi_free(X);
    bi_free(Q);
    bigint *pi_val = bi_shr(two_pi, 1);
    int rsign = sx;
    bigint *rmag;
    if (bi_cmp(R, pi_val) > 0) {
        // Shorter representative: sx*R === sx*R - sx*2pi = -sx*(2pi-R) (mod 2pi).
        rmag = bi_sub(two_pi, R);
        bi_free(R);
        rsign = -sx;
    } else {
        rmag = R;
    }
    bi_free(pi_val);
    bi_free(two_pi);
    *sign_out = bi_is_zero(rmag) ? 0 : rsign;
    *rmag_out = rmag;
    return true;
}

// atan(sign*|zmag|/2^p) * 2^p via out, returning its sign; |zmag| < 2^p
// (|z| < 1), already reduced small by the caller. Self-terminating,
// alternating -- same shape as atanh_fixed (undivided running power pk,
// term computed fresh each step), just with sign flipping each term:
// atan(z) = z - z^3/3 + z^5/5 - ...
static int atan_series(int sign, const bigint *zmag, uint32_t p, bigint **out)
{
    mpz_t z2p, pk, term, sum;
    mpz_init(z2p);
    mpz_mul(z2p, zmag, zmag);
    mpz_tdiv_q_2exp(z2p, z2p, p); // z^2 * 2^p
    mpz_init_set(pk, zmag);       // p_0 = sign * z^1 * 2^p (undivided power)
    if (sign < 0) mpz_neg(pk, pk);
    mpz_init(term);
    mpz_init(sum);
    for (uint32_t k = 0; mpz_sgn(pk) != 0; k++) {
        mpz_tdiv_q_ui(term, pk, 2 * k + 1);
        if (k % 2 == 0) mpz_add(sum, sum, term);
        else mpz_sub(sum, sum, term);
        mpz_mul(pk, pk, z2p);
        mpz_tdiv_q_2exp(pk, pk, p); // p_{k+1} = p_k * z^2
    }
    mpz_clear(z2p);
    mpz_clear(pk);
    mpz_clear(term);
    return mpz_take(sum, out);
}

// Builds a general IRRATIONAL node combining x and y (owned; for unary ops
// y is small_zero(), an immediate, so owning it is free). For IRR_DIV,
// first establishes y is nonzero via refine_sign -- giving up (see
// IRR_MAX_PREC) makes the whole division NUMBER_ERROR rather than building
// a node that could silently fail to converge later.
static number make_irr(int op, number x, number y)
{
    if (number_is_error(x) || number_is_error(y)) {
        number e = number_is_error(x) ? x : y;
        return e;
    }
    if (op == IRR_DIV) {
        int s;
        if (!refine_sign(y, &s)) {
            return err(ERR_UNDECIDABLE);
        }
    }
    num_irr *n = xmalloc(sizeof(num_irr));
    n->head.kind = KIND_IRRATIONAL;
    // op is always one of the 9 IRR_* constants above -- a closed, entirely
    // internal enum, unlike appr_prec's caller-facing unbounded range -- so
    // masking here is a legitimate narrowing, not the silent-truncation
    // hazard that ruled out masking for appr_prec earlier.
    n->op = (uint32_t)op & 0xFu;
    n->appr_sign = 0;
    n->x = x;
    n->y = y;
    mpz_init(n->appr); // lazy: allocates nothing until a value is stored
    n->appr_prec = 0;  // 0 = no memo yet
    return (number){(uint64_t)(uintptr_t)n};
}

// exp(x) * 2^w with |error| <= 4, sign always +1 (exp > 0). Range-reduces by
// repeated halving until |r| < 1 (r = x/2^k), runs the self-terminating
// series on r, then squares k times to undo the reduction: exp(x) =
// exp(r)^(2^k). Keeps ONE constant working precision (w + guard) through
// every squaring step rather than truncating between them -- each squaring
// costs about 1 bit of relative precision, so truncating early would
// compound that loss; truncate once, at the very end, down to w. Shared by
// irr_fixed's IRR_EXP case and factor_appr's FN_EXP case.
static bool exp_fixed_core(number x, uint32_t w, int *sign_out, bigint **mag_out)
{
    int32_t ex = approx_exponent(x);
    uint32_t k = ex > 0 ? (uint32_t)ex + 1 : 0;
    uint32_t P = w + 32 + k;
    int sx;
    bigint *X;
    if (!value_fixed(x, P, &sx, &X)) return false;
    bigint *R = bi_shr(X, k); // |r| * 2^P, r = x/2^k, |r| < 1
    bi_free(X);
    bigint *E;
    exp_series(sx, R, P, &E);
    bi_free(R);
    for (uint32_t i = 0; i < k; i++) {
        bigint *sq = bi_mul(E, E);
        bi_free(E);
        E = bi_shr(sq, P); // constant precision P through the chain
        bi_free(sq);
    }
    *mag_out = bi_shr(E, P - w);
    bi_free(E);
    *sign_out = 1;
    return true;
}

// ln(x) * 2^w with |error| <= 4. ln(x) = ln(m) + e*ln(2), m = x/2^e in
// [1,2), ln(m) = 2*atanh(z) with z = (m-1)/(m+1) (|z| <= 1/3, fast
// convergence). e comes from a coarse magnitude probe and is corrected by
// at most a step or two if it's off (the probe is approximate). False if
// x <= 0 (shouldn't happen: every caller has already checked positivity --
// number_ln directly, or make_real's arg > 1 canonicalization for
// factor_appr's FN_EXP case). Shared by irr_fixed's IRR_LN case and
// factor_appr's FN_LN case.
static bool ln_fixed_core(number x, uint32_t w, int *sign_out, bigint **mag_out)
{
    int32_t e = approx_exponent(x);
    uint32_t P = w + 32;
    int sx;
    bigint *X;
    if (!value_fixed(x, P, &sx, &X)) return false;
    if (sx < 0 || bi_is_zero(X)) { // ln(x<=0): shouldn't reach here
        bi_free(X);
        return false;
    }
    bigint *M = e >= 0 ? bi_shr(X, (uint32_t)e) : bi_shl(X, (uint32_t)-e);
    bi_free(X);
    int32_t blen = (int32_t)bi_bitlen(M);
    while (blen > (int32_t)P + 1) {
        bigint *t = bi_shr(M, 1);
        bi_free(M);
        M = t;
        e++;
        blen--;
    }
    while (blen < (int32_t)P + 1) {
        bigint *t = bi_shl(M, 1);
        bi_free(M);
        M = t;
        e--;
        blen++;
    }
    bigint *one = bi_from_u64(1);
    bigint *one_p = bi_shl(one, P); // 2^P, i.e. "1" at scale P
    bi_free(one);
    bigint *num = bi_sub(M, one_p); // m-1, scaled: M >= one_p since bitlen(M) == P+1
    bigint *den = bi_add(M, one_p); // m+1, scaled
    bi_free(M);
    bi_free(one_p);
    bigint *num_scaled = bi_shl(num, P);
    bi_free(num);
    bigint *z, *zrem;
    bi_divmod(num_scaled, den, &z, &zrem);
    bi_free(num_scaled);
    bi_free(den);
    bi_free(zrem);
    bigint *at = atanh_fixed(z, P);
    bi_free(z);
    bigint *ln_m = bi_shl(at, 1); // ln(m) = 2*atanh(z); ln(m) >= 0 since m >= 1
    bi_free(at);
    bigint *total;
    int sign;
    if (e == 0) {
        total = ln_m;
        sign = bi_is_zero(total) ? 0 : 1;
    } else {
        bigint *ln2 = ln2_appr(P);
        bigint *e_ln2 = bi_mul_u32(ln2, e < 0 ? (uint32_t)-e : (uint32_t)e);
        bi_free(ln2);
        sign = signed_add(1, ln_m, e < 0 ? -1 : 1, e_ln2, &total);
    }
    *mag_out = bi_shr(total, P - w);
    bi_free(total);
    *sign_out = sign;
    return true;
}

// Evaluates a DAG node to w bits, memoizing the best-known approximation
// like factor_appr does for pi/sqrt. Each op requests enough extra
// precision from its operand(s) to guarantee |error| <= 4 at w, the same
// contract value_fixed makes for every other kind.
static bool irr_fixed(num_irr *n, uint32_t w, int *sign_out, bigint **mag_out)
{
    if (n->appr_prec != 0 && n->appr_prec >= w) {
        NSTAT(irr_memo_hits);
        *sign_out = n->appr_sign;
        *mag_out = bi_shr(n->appr, n->appr_prec - w);
        return true;
    }
    NSTAT(irr_recompute[n->op]);
    uint32_t p = w + 64; // overshoot so nearby requests hit the memo, as factor_appr does
    int sign;
    bigint *mag;
    switch (n->op) {
    case IRR_ADD:
    case IRR_SUB: {
        // Both operands at p+2 bits (each with its own <=4 error), combine,
        // shift back down by 2: combined error <=8, /4 plus <1 truncation
        // keeps the total comfortably <=4.
        int sx, sy;
        bigint *vx, *vy;
        if (!value_fixed(n->x, p + 2, &sx, &vx)) return false;
        if (!value_fixed(n->y, p + 2, &sy, &vy)) {
            bi_free(vx);
            return false;
        }
        if (n->op == IRR_SUB) sy = -sy;
        bigint *sum;
        sign = signed_add(sx, vx, sy, vy, &sum);
        mag = bi_shr(sum, 2);
        bi_free(sum);
        break;
    }
    case IRR_MUL: {
        // Each operand needs extra precision to cover the OTHER's
        // magnitude, same trick real_fixed already uses for b*F.
        int32_t ex = approx_exponent(n->x), ey = approx_exponent(n->y);
        uint32_t px = p + (uint32_t)(ey > 0 ? ey : 0) + 16;
        uint32_t py = p + (uint32_t)(ex > 0 ? ex : 0) + 16;
        int sx, sy;
        bigint *vx, *vy;
        if (!value_fixed(n->x, px, &sx, &vx)) return false;
        if (!value_fixed(n->y, py, &sy, &vy)) {
            bi_free(vx);
            return false;
        }
        bigint *prod = bi_mul(vx, vy);
        bi_free(vx);
        bi_free(vy);
        mag = bi_shr(prod, px + py - p);
        bi_free(prod);
        sign = sx * sy;
        break;
    }
    case IRR_DIV: {
        // y was already proven nonzero when this node was built (make_irr).
        // py must scale with p (the target precision), not just with y's own
        // magnitude: y's RELATIVE error is ~4/(|y|*2^py), and that needs to
        // shrink below 2^-p as p grows, or the quotient's error stops
        // improving past a fixed absolute bit count no matter how much
        // precision is requested (silently wrong low digits at any p beyond
        // that). py = p + 32 (+ extra if |y| < 1, so its magnitude is still
        // resolved) keeps that relative error tiny regardless of p; x then
        // needs px = p + py bits so the quotient lands at scale p.
        int32_t ey = approx_exponent(n->y);
        uint32_t py = p + 32 + (uint32_t)(ey < 0 ? -ey : 0);
        uint32_t px = p + py;
        int sx, sy;
        bigint *vx, *vy;
        if (!value_fixed(n->x, px, &sx, &vx)) return false;
        if (!value_fixed(n->y, py, &sy, &vy)) {
            bi_free(vx);
            return false;
        }
        if (bi_is_zero(vy)) { // shouldn't happen: y was proven nonzero at construction
            bi_free(vx);
            bi_free(vy);
            return false;
        }
        bigint *q, *rem;
        bi_divmod(vx, vy, &q, &rem);
        bi_free(vx);
        bi_free(vy);
        bi_free(rem);
        mag = q;
        sign = sx * sy;
        break;
    }
    case IRR_EXP:
        if (!exp_fixed_core(n->x, p, &sign, &mag)) return false;
        break;
    case IRR_LN:
        if (!ln_fixed_core(n->x, p, &sign, &mag)) return false;
        break;
    case IRR_SIN:
    case IRR_COS: {
        // Range reduction's error scales with x's own magnitude (see
        // reduce_mod_2pi), not just with p: 2 extra guard bits per bit of
        // x's magnitude, matching the "amplified by the quotient" analysis.
        int32_t ex = approx_exponent(n->x);
        uint32_t P = p + 2 * (uint32_t)(ex > 0 ? ex : 0) + 32;
        int rsign;
        bigint *rmag;
        if (!reduce_mod_2pi(n->x, P, &rsign, &rmag)) return false;
        bigint *raw;
        if (n->op == IRR_SIN)
            sign = sin_series(rsign, rmag, P, &raw);
        else
            sign = cos_series(rmag, P, &raw);
        bi_free(rmag);
        mag = bi_shr(raw, P - p);
        bi_free(raw);
        break;
    }
    case IRR_ATAN: {
        // |x| >= 1: reduce via atan(x) = sign(x)*pi/2 - atan(1/x) first, so
        // the halving phase below always starts from |z| <= 1 -- unlike
        // SIN/COS's range reduction, this doesn't depend on x's magnitude
        // at all (a single reciprocal, not a magnitude-scaled quotient).
        int32_t ex = approx_exponent(n->x);
        bool recip = ex >= 0;
        uint32_t H = 4; // fixed halving steps; enough for any |z| <= 1 to start
        uint32_t P = p + 32 + H;
        int sx;
        bigint *Z;
        if (recip) {
            int xsign;
            bigint *X;
            if (!value_fixed(n->x, P + 32, &xsign, &X)) return false;
            bigint *one = bi_from_u64(1);
            bigint *num = bi_shl(one, 2 * P + 32); // so num/X lands at scale 2^P
            bi_free(one);
            bigint *rem;
            bi_divmod(num, X, &Z, &rem);
            bi_free(num);
            bi_free(X);
            bi_free(rem);
            sx = xsign;
        } else {
            if (!value_fixed(n->x, P, &sx, &Z)) return false;
        }
        // Halving: z' = z/(1+sqrt(1+z^2)), applied to the magnitude (the map
        // is odd, so this and tracking sign separately is equivalent to
        // applying it to signed z directly). Same "constant working
        // precision through the chain, truncate once at the end" rule as
        // EXP's squaring.
        for (uint32_t i = 0; i < H; i++) {
            bigint *z2 = bi_mul(Z, Z);
            bigint *one_a = bi_from_u64(1);
            bigint *one_2p = bi_shl(one_a, 2 * P);
            bi_free(one_a);
            bigint *sum = bi_add(z2, one_2p);
            bi_free(z2);
            bi_free(one_2p);
            bigint *sq = bi_isqrt(sum); // sqrt(1+z^2) * 2^P
            bi_free(sum);
            bigint *one_b = bi_from_u64(1);
            bigint *onep = bi_shl(one_b, P);
            bi_free(one_b);
            bigint *den = bi_add(onep, sq); // (1+sqrt(1+z^2)) * 2^P
            bi_free(sq);
            bi_free(onep);
            bigint *num = bi_shl(Z, P);
            bi_free(Z);
            bigint *rem;
            bi_divmod(num, den, &Z, &rem); // z/(1+sqrt(1+z^2)) * 2^P
            bi_free(num);
            bi_free(den);
            bi_free(rem);
        }
        bigint *series;
        int rsign = atan_series(sx, Z, P, &series);
        bi_free(Z);
        bigint *raw = bi_shl(series, H); // undo the H halvings: atan(z) = 2^H * atan(z_H)
        bi_free(series);
        bigint *total;
        if (recip) {
            bigint *pip = pi_appr(P);
            bigint *half_pi = bi_shr(pip, 1); // pi/2 * 2^P
            bi_free(pip);
            sign = signed_add(sx, half_pi, -rsign, raw, &total);
        } else {
            total = raw;
            sign = rsign;
        }
        mag = bi_shr(total, P - p);
        bi_free(total);
        break;
    }
    case IRR_SQRT: {
        // sqrt(x) via bi_isqrt (exact floor integer sqrt): fetch x at scale
        // 2*P so bi_isqrt's output lands directly at scale P (bi_isqrt(v *
        // 2^(2P)) == sqrt(v) * 2^P, the same identity IRR_ATAN's halving
        // step already relies on). bi_isqrt's own rounding error is <1;
        // the dominant error comes from x's own approximation error
        // propagating through sqrt's derivative 1/(2*sqrt(x)), which grows
        // as x shrinks toward 0 -- pad P by |exponent(x)| (the same
        // magnitude-scaled guard-bit idea EXP/LN/ATAN use above) so the
        // down-scaled result still meets the <=4 error contract.
        int32_t ex = approx_exponent(n->x);
        uint32_t guard = ex < 0 ? (uint32_t)(-ex) : 0;
        uint32_t P = p + 32 + guard;
        int sx;
        bigint *X;
        if (!value_fixed(n->x, 2 * P, &sx, &X)) return false;
        if (sx < 0) { // domain error; shouldn't reach here (checked at construction)
            bi_free(X);
            return false;
        }
        bigint *root = bi_isqrt(X);
        bi_free(X);
        mag = bi_shr(root, P - p);
        bi_free(root);
        sign = 1; // sqrt(x) > 0 always here (x > 0 was proven at construction)
        break;
    }
    default:
        abort(); // unreachable: every IRR_* op is handled above
    }
    mpz_swap(n->appr, mag);
    bi_free(mag); // now holds the previous memo
    n->appr_prec = p;
    n->appr_sign = sign < 0 ? -1 : (sign > 0 ? 1 : 0);
    *sign_out = sign;
    *mag_out = bi_shr(n->appr, p - w);
    return true;
}

// ---------------------------------------------------------------------------
// Conversion to double

// Correctly rounded sign * n/d, d > 0. n may carry a sign of its own (an
// mpq numref): only |n| is used -- bit length, limb reads, and truncating
// division are all magnitude-correct -- and the result's sign comes from
// `sign` alone.
static double rat_to_double(int sign, const bigint *n, const bigint *d)
{
    int64_t nb = bi_bitlen(n), db = bi_bitlen(d);
    // Scale so the integer quotient has 54 or 55 bits.
    int64_t shift = db + 54 - nb;
    bigint *ns, *ds;
    if (shift >= 0) {
        ns = bi_shl(n, (uint32_t)shift);
        ds = bi_copy(d);
    } else {
        ns = bi_copy(n);
        ds = bi_shl(d, (uint32_t)-shift);
    }
    bigint *q, *r;
    bi_divmod(ns, ds, &q, &r);
    uint64_t qv = 1;
    bi_fits_u64(q, &qv); // q is 54-55 bits by construction, so this always fits
    bool sticky = !bi_is_zero(r);
    bi_free(ns);
    bi_free(ds);
    bi_free(q);
    bi_free(r);

    // value = qv * 2^e2, with qv having 54 or 55 bits
    int64_t e2 = -shift;
    int qbits = 64 - __builtin_clzll(qv);
    int64_t exp_of_msb = qbits - 1 + e2; // floor(log2(value))
    if (exp_of_msb > 1024) return sign < 0 ? -(double)INFINITY : (double)INFINITY;
    if (exp_of_msb < -1075) return sign < 0 ? -0.0 : 0.0; // below half of min subnormal
    int64_t prec = 53;
    if (exp_of_msb < -1022) // subnormal range: fewer significand bits
        prec = 1075 + exp_of_msb;
    int drop = (int)(qbits - prec); // >= 1, since qbits >= 54 and prec <= 53
    uint64_t keep = qv >> drop;
    uint64_t guard = (qv >> (drop - 1)) & 1;
    bool lower = (qv & (((uint64_t)1 << (drop - 1)) - 1)) != 0 || sticky;
    if (guard && (lower || (keep & 1)))
        keep++; // may carry to prec+1 bits: a power of two, still exact
    double result = ldexp((double)keep, (int)(e2 + drop));
    return sign < 0 ? -result : result;
}

// Correctly rounded double for a real/irrational value: round both ends of
// the +-4 interval as exact rationals V/2^w; when they round identically
// the answer is certain. A real always terminates (never exactly a double
// or a tie); an irrational node might give up (NAN) past IRR_MAX_PREC bits.
// Deliberately NOT served by the double-interval fast path: deciding a
// correctly rounded double means telling values less than half an ulp
// apart, and a same-precision interval (whose two ends are themselves
// doubles, so it is at least a full ulp wide after any outward widening)
// can never fit strictly inside one rounding interval. Serving this at
// hardware speed would take double-double (compensated) interval
// arithmetic -- ~106 effective bits -- which is future work; measured, the
// always-failing attempt only made this path slower.
static double refine_to_double(number x)
{
    for (uint32_t w = 64; w <= IRR_MAX_PREC; w *= 2) {
        int s;
        bigint *v;
        if (!value_fixed(x, w, &s, &v)) return (double)NAN;
        bigint *four = bi_from_u64(4);
        if (bi_cmp(v, four) > 0) {
            bigint *lo = bi_sub(v, four);
            bigint *hi = bi_add(v, four);
            bigint *one = bi_from_u64(1);
            bigint *den = bi_shl(one, w);
            bi_free(one);
            double d1 = rat_to_double(s, lo, den);
            double d2 = rat_to_double(s, hi, den);
            bi_free(lo);
            bi_free(hi);
            bi_free(den);
            if (d1 == d2) {
                bi_free(four);
                bi_free(v);
                return d1;
            }
        }
        bi_free(four);
        bi_free(v);
    }
    return (double)NAN;
}

double number_to_double(number x)
{
    if (number_is_error(x)) return (double)NAN;
    if (number_tag(x) == TAG_SMALL) {
        // Both operands are exact in double, and IEEE division is correctly
        // rounded, so a single hardware divide gives the right answer.
        return (double)small_num(x) / (double)small_den(x);
    }
    if (is_real_kind(x) || is_irrational_kind(x)) return refine_to_double(x);
    mpq_srcptr q = number_heap(x)->q;
    return rat_to_double(mpq_sgn(q), mpq_numref(q), mpq_denref(q));
}

int64_t number_to_int64(number x, bool *ok)
{
    bool valid = false;
    int64_t result = 0;
    if (number_tag(x) == TAG_SMALL && small_den(x) == 1) {
        valid = true;
        result = small_num(x);
    } else if (number_tag(x) == TAG_POINTER && x.bits != 0 &&
               number_head(x)->kind == KIND_BIGRAT) {
        num_bigrat *p = number_heap(x);
        int sign = mpq_sgn(p->q);
        uint64_t mag;
        if (bi_is_one(mpq_denref(p->q)) && bi_fits_u64(mpq_numref(p->q), &mag)) {
            if (sign > 0 && mag <= (uint64_t)INT64_MAX) {
                valid = true;
                result = (int64_t)mag;
            } else if (sign < 0 && mag <= (uint64_t)INT64_MAX + 1) {
                valid = true;
                // -mag, avoiding signed overflow at exactly INT64_MIN.
                result = mag == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)mag;
            }
        }
    }
    if (ok) *ok = valid;
    return result;
}

// ---------------------------------------------------------------------------
// Conversion to string

// Formats N (a decimal magnitude representing round(value * 10^max_frac_digits))
// with a decimal point max_frac_digits from the right. Consumes N.
static char *format_scaled_decimal(bigint *N, int sign, uint32_t max_frac_digits)
{
    char *dec = bi_to_decimal(N);
    bool neg = sign < 0 && !bi_is_zero(N);
    bi_free(N);
    size_t len = strlen(dec);
    uint32_t fd = max_frac_digits;
    size_t ilen = len > fd ? len - fd : 1; // integer digits ("0" when none)
    char *out = xmalloc(neg + ilen + (fd ? 1 + fd : 0) + 1);
    size_t pos = 0;
    if (neg) out[pos++] = '-';
    if (len > fd) {
        memcpy(out + pos, dec, len - fd);
        pos += len - fd;
    } else {
        out[pos++] = '0';
    }
    if (fd) {
        out[pos++] = '.';
        size_t have = len > fd ? fd : len;
        memset(out + pos, '0', fd - have); // leading fractional zeros
        memcpy(out + pos + (fd - have), dec + (len - have), have);
        pos += fd;
    }
    out[pos] = '\0';
    return out;
}

// Decimal digits of a real: N = round(value * 10^d), correctly rounded by
// interval refinement (irrationals never land exactly on a rounding
// boundary). The power of ten is folded exactly into the rational
// coefficients so only one irrational factor approximation is needed.
static char *real_to_string(num_real *r, uint32_t max_frac_digits)
{
    number scale = canon_make(1, bi_pow10(max_frac_digits), bi_from_u64(1));
    number a = number_mul(r->a, scale);
    number b = number_mul(r->b, scale);

    int sign = 0;
    bigint *N = NULL;
#ifndef NUMBER_NO_DOUBLE_FILTER
    // 10^d is already folded exactly into a and b, so the hardware interval
    // of a + b*F directly bounds value*10^d; if it rounds decisively, the
    // digits are done with no bigint evaluation of F at all.
    {
        dival iv;
        int budget = DIVAL_VISIT_BUDGET;
        if (dival_of_real(r, a, b, &iv, &budget) && dival_round_int(iv, &sign, &N)) {
            NSTAT(interval_hits);
            return format_scaled_decimal(N, sign, max_frac_digits);
        }
        NSTAT(interval_fallbacks);
    }
#endif
    for (uint32_t w = 64; N == NULL; w *= 2) {
        int s;
        bigint *v;
        real_fixed(r, a, b, w, &s, &v);
        bigint *four = bi_from_u64(4);
        if (bi_cmp(v, four) <= 0) {
            // |value*10^d| <= 8*2^-w < 1/2: rounds to zero, certainly.
            N = bi_new();
            sign = 0;
        } else {
            bigint *one = bi_from_u64(1);
            bigint *half = bi_shl(one, w - 1);
            bi_free(one);
            bigint *lo = bi_sub(v, four);
            bigint *hi = bi_add(v, four);
            bigint *lo_h = bi_add(lo, half);
            bigint *hi_h = bi_add(hi, half);
            bigint *n1 = bi_shr(lo_h, w);
            bigint *n2 = bi_shr(hi_h, w);
            if (bi_cmp(n1, n2) == 0) {
                N = n1;
                sign = s;
                bi_free(n2);
            } else {
                bi_free(n1);
                bi_free(n2);
            }
            bi_free(half);
            bi_free(lo);
            bi_free(hi);
            bi_free(lo_h);
            bi_free(hi_h);
        }
        bi_free(four);
        bi_free(v);
    }
    return format_scaled_decimal(N, sign, max_frac_digits);
}

// Decimal digits of a general irrational value. Unlike real_to_string, there
// is no exact a/b to pre-scale by 10^d (no closed form), so the error bound
// itself is scaled by 10^d instead of the value: N = round(value * 10^d),
// same half-up bracket-agreement loop, just with a wider (still exact)
// error margin. Gives up (returns NULL) past IRR_MAX_PREC bits.
static bigint *irr_to_string_digits(number x, uint32_t max_frac_digits, int *sign_out)
{
#ifndef NUMBER_NO_DOUBLE_FILTER
    // Interval pre-pass, like real_to_string's. There are no exact rational
    // coefficients to fold 10^d into here, so scale the interval by 10^d in
    // double arithmetic instead -- exact for d <= 22 (10^22 = 2^22 * 5^22 is
    // the largest power of ten a double represents exactly, and every
    // smaller power along the way is exact too), and a request that deep
    // needs more precision than a double interval resolves anyway.
    if (max_frac_digits <= 22) {
        dival iv;
        int budget = DIVAL_VISIT_BUDGET;
        if (dival_of(x, &iv, &budget)) {
            double s10 = 1.0;
            for (uint32_t i = 0; i < max_frac_digits; i++) s10 *= 10.0;
            dival scaled = {ival_prev(iv.lo * s10), ival_next(iv.hi * s10)};
            bigint *N;
            if (dival_round_int(scaled, sign_out, &N)) {
                NSTAT(interval_hits);
                return N;
            }
            NSTAT(interval_fallbacks);
        }
    }
#endif
    bigint *scale = bi_pow10(max_frac_digits);
    bigint *four = bi_from_u64(4); // value_fixed's error bound, scaled by 10^d below
    bigint *err = bi_mul(four, scale);
    bi_free(four);

    bigint *N = NULL;
    for (uint32_t w = 64; N == NULL && w <= IRR_MAX_PREC; w *= 2) {
        int s;
        bigint *v;
        if (!value_fixed(x, w, &s, &v)) break;
        bigint *sv = bi_mul(v, scale); // ~ |value| * 2^w * 10^d, error <= err
        bi_free(v);
        if (bi_cmp(sv, err) <= 0) {
            // |value*10^d| <= 2*err*2^-w, tiny for any w in this loop: rounds to zero.
            bi_free(sv);
            N = bi_new();
            *sign_out = 0;
        } else {
            bigint *one = bi_from_u64(1);
            bigint *half = bi_shl(one, w - 1);
            bi_free(one);
            bigint *lo = bi_sub(sv, err);
            bigint *hi = bi_add(sv, err);
            bi_free(sv);
            bigint *lo_h = bi_add(lo, half);
            bigint *hi_h = bi_add(hi, half);
            bigint *n1 = bi_shr(lo_h, w);
            bigint *n2 = bi_shr(hi_h, w);
            if (bi_cmp(n1, n2) == 0) {
                N = n1;
                *sign_out = s;
                bi_free(n2);
            } else {
                bi_free(n1);
                bi_free(n2);
            }
            bi_free(half);
            bi_free(lo);
            bi_free(hi);
            bi_free(lo_h);
            bi_free(hi_h);
        }
    }
    bi_free(scale);
    bi_free(err);
    return N;
}

static char *irr_to_string(number x, uint32_t max_frac_digits)
{
    int sign = 0;
    bigint *N = irr_to_string_digits(x, max_frac_digits, &sign);
    if (N == NULL) return xstrdup("(error)"); // gave up within the precision cap
    return format_scaled_decimal(N, sign, max_frac_digits);
}

// TAG_SMALL fast path for number_to_string: den <= SMALL_DEN_MAX (2^30 - 1)
// keeps every intermediate here comfortably inside uint64_t with room to
// spare (r < den, so r*10^9 < 2^30*10^9 ~= 1.07e18, vs. UINT64_MAX ~=
// 1.84e19) -- unlike number_from_string_fast, there's no overflow case to
// bail out of; this always applies when x is TAG_SMALL. Mirrors the
// general bigint-based algorithm below digit for digit (same 9-digit
// batching, same round-half-even, same carry cascade), just entirely off
// the heap for the digit generation itself.
static char *number_to_string_small(number x, uint32_t max_frac_digits, bool *is_exact)
{
    int64_t num = small_num(x);
    uint64_t den = small_den(x);
    bool neg = num < 0;
    uint64_t n = neg ? (uint64_t)(-num) : (uint64_t)num;

    uint64_t Q = n / den, r = n % den;
    char int_digits[20]; // a uint64_t magnitude is at most 20 decimal digits
    size_t ilen = 0;
    if (Q == 0) {
        int_digits[ilen++] = '0';
    } else {
        char tmp[20];
        size_t tn = 0;
        while (Q) {
            tmp[tn++] = (char)('0' + Q % 10);
            Q /= 10;
        }
        while (tn) int_digits[ilen++] = tmp[--tn];
    }

    static const uint64_t POW10_U64[10] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
    };
    char *frac = xmalloc(max_frac_digits ? max_frac_digits : 1);
    uint32_t fn = 0;
    while (fn < max_frac_digits && r != 0) {
        uint32_t batch = max_frac_digits - fn < 9 ? max_frac_digits - fn : 9;
        uint64_t scaled = r * POW10_U64[batch];
        uint64_t q = scaled / den;
        r = scaled % den;
        for (uint32_t i = 0; i < batch; i++)
            frac[fn + i] = (char)('0' + (q / POW10_U64[batch - 1 - i]) % 10);
        fn += batch;
        if (r == 0) // see number_to_string's identical trim: terminated mid-batch
            while (frac[fn - 1] == '0') fn--;
    }
    bool exact = r == 0;

    // Round half-even on the last emitted digit.
    bool round_up = false;
    if (!exact) {
        if (r * 2 > den) {
            round_up = true;
        } else if (r * 2 == den) {
            char last = fn ? frac[fn - 1] : int_digits[ilen - 1];
            round_up = ((last - '0') & 1) != 0;
        }
    }

    // One contiguous digit buffer with a spare leading '0' to absorb carries.
    char *digits = xmalloc(1 + ilen + fn);
    digits[0] = '0';
    memcpy(digits + 1, int_digits, ilen);
    memcpy(digits + 1 + ilen, frac, fn);
    if (round_up) {
        size_t i = ilen + fn;
        while (digits[i] == '9') digits[i--] = '0';
        digits[i]++;
    }
    bool carried = digits[0] != '0';

    char *out = xmalloc(neg + carried + ilen + (fn ? 1 + fn : 0) + 1);
    size_t pos = 0;
    if (neg) out[pos++] = '-';
    if (carried) out[pos++] = digits[0];
    memcpy(out + pos, digits + 1, ilen);
    pos += ilen;
    if (fn) {
        out[pos++] = '.';
        memcpy(out + pos, digits + 1 + ilen, fn);
        pos += fn;
    }
    out[pos] = '\0';

    if (is_exact) *is_exact = exact;
    return out;
}

char *number_to_string(number x, uint32_t max_frac_digits, bool *is_exact)
{
    if (is_exact) *is_exact = true;
    if (number_is_error(x)) return xstrdup("(error)");
    if (is_real_kind(x)) {
        if (is_exact) *is_exact = false; // reals are irrational: never exact
        return real_to_string(as_real(x), max_frac_digits);
    }
    if (is_irrational_kind(x)) {
        if (is_exact) *is_exact = false;
        return irr_to_string(x, max_frac_digits);
    }
    if (number_tag(x) == TAG_SMALL) return number_to_string_small(x, max_frac_digits, is_exact);

    mpq_srcptr xq = number_heap(x)->q; // heap bigrat: TAG_SMALL handled above
    mpz_srcptr den = mpq_denref(xq);
    bool neg = mpq_sgn(xq) < 0; // zero is never heap
    mpz_t Q, r, qd;
    mpz_init(Q);
    mpz_init(r);
    mpz_init(qd);
    mpz_tdiv_qr(Q, r, mpq_numref(xq), den);
    mpz_abs(Q, Q); // O(1) sign strips: digit generation runs on magnitudes,
    mpz_abs(r, r); // with `neg` captured above
    char *int_digits = bi_to_decimal(Q);
    size_t ilen = strlen(int_digits);

    // Extract up to 9 fractional digits per bigint divmod instead of 1 --
    // r < u.den is the loop invariant (r is always a remainder), so
    // r*10^batch < u.den*10^batch, i.e. the quotient below always has at
    // most `batch` digits: zero-padding it out to `batch` digits is exact,
    // not just a bound. 9 is the most digits a uint32 quotient can safely
    // hold (10^9 < 2^32) while still fitting bi_mul_u32's uint32 multiplier.
    static const uint32_t POW10_U32[10] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
    };
    char *frac = xmalloc(max_frac_digits ? max_frac_digits : 1);
    uint32_t fn = 0;
    while (fn < max_frac_digits && mpz_sgn(r) != 0) {
        uint32_t batch = max_frac_digits - fn < 9 ? max_frac_digits - fn : 9;
        mpz_mul_ui(r, r, POW10_U32[batch]);
        mpz_tdiv_qr(qd, r, r, den); // in-place: buffers reused every batch
        uint32_t q = (uint32_t)mpz_get_ui(qd); // < 10^batch <= 10^9, fits u32
        for (uint32_t i = 0; i < batch; i++)
            frac[fn + i] = (char)('0' + (q / POW10_U32[batch - 1 - i]) % 10);
        fn += batch;
        // The fraction terminated exactly inside this batch: q's low digits
        // are correct trailing-zero padding *past* the true last digit
        // (single-digit-at-a-time division would have stopped there), not
        // significant output -- trim back to it. There's always a nonzero
        // digit to stop at: if the whole batch were zero, r would already
        // have been zero going in, contradicting the loop condition above.
        if (mpz_sgn(r) == 0)
            while (frac[fn - 1] == '0') fn--;
    }
    bool exact = mpz_sgn(r) == 0;

    // Round half-even on the last emitted digit.
    bool round_up = false;
    if (!exact) {
        mpz_mul_2exp(r, r, 1);
        int c = mpz_cmp(r, den);
        if (c > 0) {
            round_up = true;
        } else if (c == 0) {
            char last = fn ? frac[fn - 1] : int_digits[ilen - 1];
            round_up = ((last - '0') & 1) != 0;
        }
    }
    mpz_clear(Q);
    mpz_clear(r);
    mpz_clear(qd);

    // One contiguous digit buffer with a spare leading '0' to absorb carries.
    char *digits = xmalloc(1 + ilen + fn);
    digits[0] = '0';
    memcpy(digits + 1, int_digits, ilen);
    memcpy(digits + 1 + ilen, frac, fn);
    if (round_up) {
        size_t i = ilen + fn;
        while (digits[i] == '9') digits[i--] = '0';
        digits[i]++;
    }
    bool carried = digits[0] != '0';

    char *out = xmalloc(neg + carried + ilen + (fn ? 1 + fn : 0) + 1);
    size_t pos = 0;
    if (neg) out[pos++] = '-';
    if (carried) out[pos++] = digits[0];
    memcpy(out + pos, digits + 1, ilen);
    pos += ilen;
    if (fn) {
        out[pos++] = '.';
        memcpy(out + pos, digits + 1 + ilen, fn);
        pos += fn;
    }
    out[pos] = '\0';

    if (is_exact) *is_exact = exact;
    return out;
}

// ---------------------------------------------------------------------------
// Symbolic form

// A rational as "42", "-7/2", ...
static char *rational_symbolic(number x)
{
    if (number_tag(x) == TAG_SMALL) {
        // Small rationals are already canonical (number-design.md) and fit an
        // int64/uint32 outright -- format directly instead of routing
        // through mpz just to convert straight back to decimal.
        int64_t num = small_num(x);
        uint64_t den = small_den(x);
        return den == 1 ? xsprintf("%lld", (long long)num)
                         : xsprintf("%lld/%llu", (long long)num, (unsigned long long)den);
    }
    mpq_srcptr q = number_heap(x)->q; // heap bigrat: TAG_SMALL handled above
    char *num = mpz_mag_decimal(mpq_numref(q));
    const char *minus = mpq_sgn(q) < 0 ? "-" : ""; // zero is never heap
    char *result;
    if (bi_is_one(mpq_denref(q))) {
        result = xsprintf("%s%s", minus, num);
    } else {
        char *den = bi_to_decimal(mpq_denref(q));
        result = xsprintf("%s%s/%s", minus, num, den);
    }
    return result;
}

static const char *irr_op_name(int op)
{
    switch (op) {
    case IRR_SIN: return "sin";
    case IRR_COS: return "cos";
    case IRR_EXP: return "exp";
    case IRR_LN: return "ln";
    case IRR_ATAN: return "atan";
    case IRR_SQRT: return "sqrt";
    default: return NULL; // ADD/SUB/MUL/DIV print as infix, not a function call
    }
}

#ifdef NUMBER_STATS
void number_stats_dump(FILE *out)
{
    static const char *const op_names[STAT_OP_COUNT] = {
        [STAT_ADD] = "add", [STAT_SUB] = "sub", [STAT_MUL] = "mul",
        [STAT_DIV] = "div", [STAT_COMPARE] = "compare",
    };
    fprintf(out, "number_stats:\n  calls:");
    for (int i = 0; i < STAT_OP_COUNT; i++)
        fprintf(out, " %s=%llu", op_names[i], (unsigned long long)number_stats.ops[i]);
    uint64_t dispatched = number_stats.tier1_fastpath + number_stats.tier1_general +
                           number_stats.tier2 + number_stats.tier3;
    fprintf(out, "\n  tier1: fastpath=%llu general=%llu\n  tier2 (bigrat): %llu (u128 fast path: %llu)\n"
                 "  tier3 (real/irrational): %llu\n  dispatched total: %llu\n"
                 "  promotions (tier1 -> tier2): %llu\n  bi_allocs: %llu\n"
                 "  irr nodes: memo_hits=%llu recompute:",
            (unsigned long long)number_stats.tier1_fastpath,
            (unsigned long long)number_stats.tier1_general,
            (unsigned long long)number_stats.tier2, (unsigned long long)number_stats.tier2_u128_fast,
            (unsigned long long)number_stats.tier3,
            (unsigned long long)dispatched, (unsigned long long)number_stats.promotions,
            (unsigned long long)number_stats.bi_allocs,
            (unsigned long long)number_stats.irr_memo_hits);
    for (int op = 0; op < IRR_DIV + 1; op++) {
        const char *name = irr_op_name(op);
        if (!name)
            name = op == IRR_ADD ? "add" : op == IRR_SUB ? "sub" : op == IRR_MUL ? "mul" : "div";
        fprintf(out, " %s=%llu", name, (unsigned long long)number_stats.irr_recompute[op]);
    }
    fprintf(out, "\n  double-interval filter: hits=%llu fallbacks=%llu\n",
            (unsigned long long)number_stats.interval_hits,
            (unsigned long long)number_stats.interval_fallbacks);
}
#endif

// Whether a and b certainly denote the same value, decided cheaply and
// syntactically: bitwise-identical words (the same immediate, or the same
// shared node -- e.g. every pi is the cached singleton), equal rationals
// (exact and cheap, since both rational tiers are canonical), or
// same-shaped real/irrational nodes over such parts (two separately-built
// sin(2) nodes are distinct pointers but identical shapes). Never refines
// approximations and never loops, unlike full number_equal on two
// irrationals; "false" just means "not provably the same by inspection",
// which the power-collapsing display below treats as distinct factors.
static bool same_displayed_value(number a, number b)
{
    if (a.bits == b.bits) return true;
    if (is_real_kind(a) && is_real_kind(b)) {
        num_real *ra = as_real(a), *rb = as_real(b);
        return ra->fn == rb->fn && same_displayed_value(ra->a, rb->a) &&
               same_displayed_value(ra->b, rb->b) &&
               (ra->fn == FN_PI || same_displayed_value(ra->arg, rb->arg));
    }
    if (is_irrational_kind(a) && is_irrational_kind(b)) {
        num_irr *na = as_irr(a), *nb = as_irr(b);
        return na->op == nb->op && same_displayed_value(na->x, nb->x) &&
               same_displayed_value(na->y, nb->y);
    }
    if (number_is_rational(a) && number_is_rational(b)) return number_equal(a, b);
    return false;
}

// The leaves of a multiplication chain: nested IRR_MUL nodes flattened in
// left-to-right order, so ((x*x)*x) and (x*(x*x)) both yield [x, x, x].
static size_t count_mul_leaves(number x)
{
    if (is_irrational_kind(x) && as_irr(x)->op == IRR_MUL)
        return count_mul_leaves(as_irr(x)->x) + count_mul_leaves(as_irr(x)->y);
    return 1;
}

static void fill_mul_leaves(number x, number *leaves, size_t *pos)
{
    if (is_irrational_kind(x) && as_irr(x)->op == IRR_MUL) {
        fill_mul_leaves(as_irr(x)->x, leaves, pos);
        fill_mul_leaves(as_irr(x)->y, leaves, pos);
        return;
    }
    leaves[(*pos)++] = x;
}

// Whether every leaf of the multiplication chain under x matches ref --
// i.e. the chain displays as a single power like "pi^3".
static bool mul_leaves_all_same(number x, number ref)
{
    if (is_irrational_kind(x) && as_irr(x)->op == IRR_MUL)
        return mul_leaves_all_same(as_irr(x)->x, ref) &&
               mul_leaves_all_same(as_irr(x)->y, ref);
    return same_displayed_value(x, ref);
}

// How loosely the printed form of x binds at its top level, for deciding
// whether an infix context must parenthesize it as an operand. Ordered
// from tightest to loosest:
//   RENDER_ATOM: a single token or self-delimited form -- "42", "pi",
//     "sqrt(6)", "sin(2)", "pi^3"
//   RENDER_PRODUCT: a top-level '*' or '/' -- "1/3", "2*pi", "sqrt(5)/2",
//     "pi^2*sin(2)", "x/y"
//   RENDER_SUM: a top-level '+'/'-' or a leading sign -- "1 + pi", "-pi",
//     "-2", "x + y"
enum { RENDER_ATOM, RENDER_PRODUCT, RENDER_SUM };

static int render_level(number x)
{
    if (is_irrational_kind(x)) {
        num_irr *n = as_irr(x);
        switch (n->op) {
        case IRR_ADD:
        case IRR_SUB: return RENDER_SUM;
        case IRR_DIV: return RENDER_PRODUCT; // "x/y" (TeX \frac never
                                              // needs wrapping, and PRODUCT
                                              // never triggers one there)
        case IRR_MUL: {
            // "pi^3" binds like an atom; "pi^2*sin(2)" is a product
            number ref = n->x;
            while (is_irrational_kind(ref) && as_irr(ref)->op == IRR_MUL)
                ref = as_irr(ref)->x;
            return mul_leaves_all_same(x, ref) ? RENDER_ATOM : RENDER_PRODUCT;
        }
        default: return RENDER_ATOM; // function calls: "sin(2)", "exp(2)"
        }
    }
    if (is_real_kind(x)) {
        num_real *r = as_real(x);
        if (!number_is_zero(r->a) || number_is_negative(r->b)) return RENDER_SUM;
        // A bare positive term: "pi"/"sqrt(n)" when b == 1, else "2*pi",
        // "sqrt(5)/2", ...
        return number_equal(r->b, NUMBER_ONE) ? RENDER_ATOM : RENDER_PRODUCT;
    }
    // Rational tier
    if (number_is_negative(x)) return RENDER_SUM;
    if (number_tag(x) == TAG_SMALL) return small_den(x) == 1 ? RENDER_ATOM : RENDER_PRODUCT;
    return bi_is_one(mpq_denref(number_heap(x)->q)) ? RENDER_ATOM : RENDER_PRODUCT;
}

// Wrap an already-malloc'd rendering in parentheses, consuming it.
static char *wrap_parens(char *s)
{
    char *wrapped = xsprintf("(%s)", s);
    return wrapped;
}

// Whether base's rendering must be wrapped in parentheses before an
// exponent is attached to it: anything looser than an atom, plus two
// TeX-only cases -- e^{x}^{2} is a double-superscript error and
// \frac{...}{...}^{2} misreads (their symbolic spellings "exp(x)" and
// "x/y" are fine as-is).
static bool pow_base_needs_parens(number base, bool tex)
{
    if (tex && is_irrational_kind(base)) {
        uint32_t op = as_irr(base)->op;
        if (op == IRR_EXP || op == IRR_DIV) return true;
    }
    return render_level(base) != RENDER_ATOM;
}

// A multiplication chain with repeated factors collapsed into powers:
// x*x*x displays as "x^3" (or "x^{3}" in TeX) rather than "x*x*x".
// Factors are grouped by same_displayed_value in first-appearance order --
// commutativity makes the reordering sound. No outer parens: contexts that
// embed this as an operand add them per render_level. Shared by the
// symbolic and TeX renderings; only the spelling differs.
static char *mul_chain_str(num_irr *n, bool tex)
{
    size_t total = count_mul_leaves(n->x) + count_mul_leaves(n->y);
    number *leaves = xmalloc(total * sizeof(number));
    size_t pos = 0;
    fill_mul_leaves(n->x, leaves, &pos);
    fill_mul_leaves(n->y, leaves, &pos);

    char *acc = NULL;
    for (size_t i = 0; i < total; i++) {
        if (leaves[i].bits == 0) continue; // folded into an earlier factor
        size_t power = 1;
        for (size_t j = i + 1; j < total; j++) {
            if (leaves[j].bits != 0 && same_displayed_value(leaves[i], leaves[j])) {
                leaves[j].bits = 0; // no real number has word 0 (that's a NULL pointer)
                power++;
            }
        }
        char *base = tex ? number_to_tex(leaves[i]) : number_to_symbolic(leaves[i]);
        char *piece = base;
        if (power > 1) {
            bool parens = pow_base_needs_parens(leaves[i], tex);
            if (tex)
                piece = parens ? xsprintf("(%s)^{%zu}", base, power)
                               : xsprintf("%s^{%zu}", base, power);
            else
                piece = parens ? xsprintf("(%s)^%zu", base, power)
                               : xsprintf("%s^%zu", base, power);
        } else if (render_level(leaves[i]) == RENDER_SUM) {
            // An additive or sign-leading factor misreads bare in a
            // product: (1+pi)*x is "(1 + pi)*x", not "1 + pi*x"
            piece = wrap_parens(base);
        }
        if (!acc) {
            acc = piece;
        } else {
            // '*' prints tight, matching the closed forms ("2*pi"); TeX
            // keeps spaces around \cdot only to delimit the macro name
            // (math mode ignores them)
            char *joined = tex ? xsprintf("%s \\cdot %s", acc, piece)
                               : xsprintf("%s*%s", acc, piece);
            acc = joined;
        }
    }
    return acc;
}

// A general IRRATIONAL node has no closed algebraic form (that's why it's
// here), but number_to_symbolic's contract is "always exact" -- satisfied
// by printing an exact expression-tree description instead of a decimal
// approximation, e.g. "sin(2)", "pi + sqrt(2)", "pi^2".
static char *irr_symbolic(num_irr *n)
{
    if (n->op == IRR_MUL) return mul_chain_str(n, false);
    char *xs = number_to_symbolic(n->x);
    const char *fname = irr_op_name(n->op);
    if (fname) {
        char *result = xsprintf("%s(%s)", fname, xs);
        return result;
    }
    char *ys = number_to_symbolic(n->y);
    // A right operand that renders as a sum misreads after any of these
    // operators ("x - 1 + pi"); after '/' even a product does ("x/2*pi").
    // Left operands only misread when a sum meets '/' ("1 + pi/x").
    if (render_level(n->y) >= (n->op == IRR_DIV ? RENDER_PRODUCT : RENDER_SUM))
        ys = wrap_parens(ys);
    if (n->op == IRR_DIV && render_level(n->x) == RENDER_SUM)
        xs = wrap_parens(xs);
    // '/' prints tight and '+'/'-' spaced, matching the closed forms
    // ("pi/3", "1/2 + sqrt(5)/2")
    char *result = n->op == IRR_DIV
                        ? xsprintf("%s/%s", xs, ys)
                        : xsprintf("%s %s %s", xs, n->op == IRR_ADD ? "+" : "-", ys);
    return result;
}

char *number_to_symbolic(number x)
{
    if (number_is_error(x)) return xstrdup("(error)");
    if (is_irrational_kind(x)) return irr_symbolic(as_irr(x));
    if (!is_real_kind(x)) return rational_symbolic(x);

    // a + b*F prints as e.g. "pi", "2*pi", "-sqrt(2)", "3 - sqrt(2)",
    // "sqrt(5)/2", "1/2 + sqrt(5)/2": the coefficient b = +-p/q wraps the
    // factor as [p*]F[/q], and a nonzero rational part is printed in front.
    num_real *r = as_real(x);
    char *factor;
    if (r->fn == FN_PI) {
        factor = xstrdup("pi");
    } else if (r->fn == FN_SQRT) {
        char *rad = rational_symbolic(r->arg);
        factor = xsprintf("sqrt(%s)", rad);
    } else if (r->fn == FN_LN) {
        char *arg_s = rational_symbolic(r->arg);
        factor = xsprintf("ln(%s)", arg_s);
    } else { // FN_EXP
        char *arg_s = rational_symbolic(r->arg);
        factor = xsprintf("exp(%s)", arg_s);
    }
    mpq_view vb;
    mpq_srcptr qb = number_mpq_view(r->b, &vb);
    bool bneg = mpq_sgn(qb) < 0; // b is nonzero by the real-form invariant
    char *term;
    if (mpz_cmpabs_ui(mpq_numref(qb), 1) == 0) { // |num| == 1
        term = factor;
    } else {
        char *p = mpz_mag_decimal(mpq_numref(qb));
        term = xsprintf("%s*%s", p, factor);
    }
    if (!bi_is_one(mpq_denref(qb))) {
        char *q = bi_to_decimal(mpq_denref(qb));
        char *t = xsprintf("%s/%s", term, q);
        term = t;
    }
    char *result;
    if (number_is_zero(r->a)) {
        result = bneg ? xsprintf("-%s", term) : xstrdup(term);
    } else {
        char *a_str = rational_symbolic(r->a);
        result = xsprintf("%s %c %s", a_str, bneg ? '-' : '+', term);
    }
    return result;
}

// A rational as TeX: "42", "-\frac{7}{2}" (the sign stays outside the
// fraction, where TeX convention puts it).
static char *rational_tex(number x)
{
    if (number_tag(x) == TAG_SMALL) {
        int64_t num = small_num(x);
        uint64_t den = small_den(x);
        if (den == 1) return xsprintf("%lld", (long long)num);
        return xsprintf("%s\\frac{%lld}{%llu}", num < 0 ? "-" : "",
                        (long long)(num < 0 ? -num : num), (unsigned long long)den);
    }
    mpq_srcptr q = number_heap(x)->q; // heap bigrat: TAG_SMALL handled above
    char *num = mpz_mag_decimal(mpq_numref(q));
    const char *minus = mpq_sgn(q) < 0 ? "-" : ""; // zero is never heap
    char *result;
    if (bi_is_one(mpq_denref(q))) {
        result = xsprintf("%s%s", minus, num);
    } else {
        char *den = bi_to_decimal(mpq_denref(q));
        result = xsprintf("%s\\frac{%s}{%s}", minus, num, den);
    }
    return result;
}

// TeX for a general IRRATIONAL node, mirroring irr_symbolic: an exact
// expression-tree description, just in TeX spelling — "\sin(2)",
// "(\pi + \sqrt{2})", "\frac{\ln(5)}{\ln(10)}".
static char *irr_tex(num_irr *n)
{
    if (n->op == IRR_MUL) return mul_chain_str(n, true);
    char *xs = number_to_tex(n->x);
    char *result;
    switch (n->op) {
    case IRR_SQRT: result = xsprintf("\\sqrt{%s}", xs); break;
    case IRR_EXP: result = xsprintf("e^{%s}", xs); break;
    case IRR_SIN: result = xsprintf("\\sin(%s)", xs); break;
    case IRR_COS: result = xsprintf("\\cos(%s)", xs); break;
    case IRR_LN: result = xsprintf("\\ln(%s)", xs); break;
    case IRR_ATAN: result = xsprintf("\\arctan(%s)", xs); break;
    case IRR_DIV: {
        char *ys = number_to_tex(n->y);
        result = xsprintf("\\frac{%s}{%s}", xs, ys);
        break;
    }
    default: { // ADD/SUB: infix, same operand rule as irr_symbolic
        char *ys = number_to_tex(n->y);
        // A sum-rendering right operand misreads bare ("x - 1 + \pi").
        // \frac division needs no wrapping.
        if (render_level(n->y) == RENDER_SUM) ys = wrap_parens(ys);
        const char *op_str = n->op == IRR_ADD ? "+" : "-";
        result = xsprintf("%s %s %s", xs, op_str, ys);
        break;
    }
    }
    return result;
}

char *number_to_tex(number x)
{
    if (number_is_error(x)) return xstrdup("\\text{error}");
    if (is_irrational_kind(x)) return irr_tex(as_irr(x));
    if (!is_real_kind(x)) return rational_tex(x);

    // a + b*F, same structure as number_to_symbolic: the coefficient
    // b = +-p/q wraps the factor as \frac{[p]F}{q} (juxtaposition, no '*':
    // "2\pi", "3\sqrt{2}"), and a nonzero rational part goes in front.
    num_real *r = as_real(x);
    char *factor;
    if (r->fn == FN_PI) {
        factor = xstrdup("\\pi");
    } else if (r->fn == FN_SQRT) {
        char *rad = rational_symbolic(r->arg); // a positive integer: plain digits
        factor = xsprintf("\\sqrt{%s}", rad);
    } else if (r->fn == FN_LN) {
        char *arg_s = rational_tex(r->arg);
        factor = xsprintf("\\ln(%s)", arg_s);
    } else { // FN_EXP: same "e^{...}" spelling irr_tex uses for IRR_EXP
        char *arg_s = rational_tex(r->arg);
        factor = xsprintf("e^{%s}", arg_s);
    }
    mpq_view vb;
    mpq_srcptr qb = number_mpq_view(r->b, &vb);
    bool bneg = mpq_sgn(qb) < 0; // b is nonzero by the real-form invariant
    char *term;
    if (mpz_cmpabs_ui(mpq_numref(qb), 1) == 0) { // |num| == 1
        term = factor;
    } else {
        char *p = mpz_mag_decimal(mpq_numref(qb));
        term = xsprintf("%s%s", p, factor);
    }
    if (!bi_is_one(mpq_denref(qb))) {
        char *q = bi_to_decimal(mpq_denref(qb));
        char *t = xsprintf("\\frac{%s}{%s}", term, q);
        term = t;
    }
    char *result;
    if (number_is_zero(r->a)) {
        result = bneg ? xsprintf("-%s", term) : xstrdup(term);
    } else {
        char *a_str = rational_tex(r->a);
        result = xsprintf("%s %c %s", a_str, bneg ? '-' : '+', term);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Parsing

#define MAX_PARSE_EXPONENT 999999L

// Fast path for number_from_string: parses a literal directly into int64
// accumulators, mirroring the immediate-tier fast paths every arithmetic
// operator already takes (see number_addsub/number_mul/number_div) instead
// of unconditionally building through the bigint accumulator below. The
// exact path still handles every case correctly on its own, so this only
// needs to be conservative, never exhaustive: on any accumulator overflow,
// unexpected character, or malformed input, it bails out (false, no side
// effects via *out) and the caller re-parses with the exact path, which
// produces the right value (or the right parse error) either way.
static bool number_from_string_fast(const char *str, number *out)
{
    const char *s = str;
    while (isspace((unsigned char)*s)) s++;
    int sign = 1;
    if (*s == '+' || *s == '-') {
        if (*s == '-') sign = -1;
        s++;
    }
    int64_t mant = 0;
    bool any_digits = false;
    while (isdigit((unsigned char)*s)) {
        if (__builtin_mul_overflow(mant, (int64_t)10, &mant) ||
            __builtin_add_overflow(mant, (int64_t)(*s - '0'), &mant))
            return false;
        any_digits = true;
        s++;
    }
    if (*s == '/') { // rational form: digits '/' digits
        s++;
        int64_t den = 0;
        bool any_den = false;
        while (isdigit((unsigned char)*s)) {
            if (__builtin_mul_overflow(den, (int64_t)10, &den) ||
                __builtin_add_overflow(den, (int64_t)(*s - '0'), &den))
                return false;
            any_den = true;
            s++;
        }
        while (isspace((unsigned char)*s)) s++;
        if (!any_digits || !any_den || *s != '\0') return false; // malformed/empty: let the exact path report it
        *out = number_from_ratio64(sign * mant, den);
        return true;
    }
    long frac_digits = 0;
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) {
            if (__builtin_mul_overflow(mant, (int64_t)10, &mant) ||
                __builtin_add_overflow(mant, (int64_t)(*s - '0'), &mant))
                return false;
            frac_digits++;
            any_digits = true;
            s++;
        }
    }
    long expo = 0;
    if (any_digits && (*s == 'e' || *s == 'E')) {
        s++;
        int esign = 1;
        if (*s == '+' || *s == '-') {
            if (*s == '-') esign = -1;
            s++;
        }
        bool any_exp = false;
        while (isdigit((unsigned char)*s)) {
            if (expo > MAX_PARSE_EXPONENT) return false; // large exponent: let the exact path scale it (or error)
            expo = expo * 10 + (*s - '0');
            any_exp = true;
            s++;
        }
        if (!any_exp) return false; // malformed: let the exact path report it
        expo *= esign;
    }
    while (isspace((unsigned char)*s)) s++;
    if (!any_digits || *s != '\0') return false; // malformed/empty: let the exact path report it

    // value = mant * 10^(expo - frac_digits); scale (a single power of 10)
    // must itself fit in int64, same as mant -- past ~18 digits of combined
    // magnitude this overflows and bails, same as the digit loops above.
    long p10 = expo - frac_digits;
    long mag = p10 < 0 ? -p10 : p10;
    int64_t scale = 1;
    for (long i = 0; i < mag; i++) {
        if (__builtin_mul_overflow(scale, (int64_t)10, &scale)) return false;
    }
    if (p10 >= 0) {
        int64_t num;
        if (__builtin_mul_overflow(mant, scale, &num)) return false;
        *out = number_from_ratio64(sign * num, 1);
    } else {
        *out = number_from_ratio64(sign * mant, scale);
    }
    return true;
}

// Appends a run of decimal digits to *acc, batching up to 9 digits into a
// uint32 chunk per bi_muladd_u32 pass (10^9 is the largest power of ten
// whose chunk still fits a uint32) instead of one pass per digit -- the same
// batching bi_to_decimal uses in the other direction. Advances *sp past the
// run and returns how many digits it consumed (0 if *sp starts on a
// non-digit).
// Appends the digit run at *sp to *acc (acc = acc*10^n + run), advancing
// *sp past it; returns the digit count. The run parses via mpz_set_str
// (subquadratic divide-and-conquer, same family as mpz_get_str on output),
// so a huge literal costs what GMP's own conversion costs, not O(n^2).
static long parse_digit_run(bigint **acc, const char **sp)
{
    const char *start = *sp, *s = start;
    while (isdigit((unsigned char)*s)) s++;
    long count = s - start;
    *sp = s;
    if (count == 0) return 0;
    char *buf = xmalloc((size_t)count + 1);
    memcpy(buf, start, (size_t)count);
    buf[count] = '\0';
    mpz_t run;
    mpz_init(run);
    mpz_set_str(run, buf, 10);
    if (!bi_is_zero(*acc)) { // continuing across a '.': shift what's there
        mpz_t p;
        mpz_init(p);
        mpz_ui_pow_ui(p, 10, (unsigned long)count);
        mpz_mul(*acc, *acc, p);
        mpz_clear(p);
    }
    mpz_add(*acc, *acc, run);
    mpz_clear(run);
    return count;
}

number number_from_string(const char *str)
{
    if (!str) return err(ERR_PARSE);
    number fast;
    if (number_from_string_fast(str, &fast)) return fast;
    const char *s = str;
    while (isspace((unsigned char)*s)) s++;
    int sign = 1;
    if (*s == '+' || *s == '-') {
        if (*s == '-') sign = -1;
        s++;
    }

    bigint *mant = bi_new();
    bool any_digits = parse_digit_run(&mant, &s) > 0;

    if (*s == '/') { // rational form: digits '/' digits
        s++;
        bigint *den = bi_new();
        bool any_den = parse_digit_run(&den, &s) > 0;
        while (isspace((unsigned char)*s)) s++;
        if (!any_digits || !any_den || *s != '\0') {
            bi_free(mant);
            bi_free(den);
            return err(ERR_PARSE);
        }
        return canon_make(sign, mant, den);
    }

    long frac_digits = 0;
    if (*s == '.') {
        s++;
        frac_digits = parse_digit_run(&mant, &s);
        if (frac_digits > 0) any_digits = true;
    }
    long expo = 0;
    if (any_digits && (*s == 'e' || *s == 'E')) {
        s++;
        int esign = 1;
        if (*s == '+' || *s == '-') {
            if (*s == '-') esign = -1;
            s++;
        }
        bool any_exp = false;
        while (isdigit((unsigned char)*s)) {
            if (expo <= MAX_PARSE_EXPONENT) expo = expo * 10 + (*s - '0');
            any_exp = true;
            s++;
        }
        if (!any_exp || expo > MAX_PARSE_EXPONENT) {
            bi_free(mant);
            return err(ERR_PARSE);
        }
        expo *= esign;
    }
    while (isspace((unsigned char)*s)) s++;
    if (!any_digits || *s != '\0') {
        bi_free(mant);
        return err(ERR_PARSE);
    }

    // value = mant * 10^(expo - frac_digits)
    long p10 = expo - frac_digits;
    bigint *num, *den;
    if (p10 >= 0) {
        bigint *scale = bi_pow10((uint32_t)p10);
        num = bi_mul(mant, scale);
        bi_free(mant);
        bi_free(scale);
        den = bi_from_u64(1);
    } else {
        num = mant;
        den = bi_pow10((uint32_t)-p10);
    }
    return canon_make(sign, num, den);
}

// The named, exact decimal-literal constructor (see number.h): the answer to
// "how do I turn the literal 3.15 into a number without number_from_double's
// silent double round-trip." Deliberately delegates to number_from_string's
// exact decimal parse -- the exact machinery already lives there; this is the
// discoverable name for it -- but rejects the "22/7" ratio form up front, so
// "decimal" is honest (a ratio isn't decimal notation; callers who want one
// reach for number_from_ratio or number_from_string).
number number_from_decimal(const char *str)
{
    if (!str) return err(ERR_PARSE);
    for (const char *p = str; *p; p++)
        if (*p == '/') return err(ERR_PARSE);
    return number_from_string(str);
}

// ---------------------------------------------------------------------------
// Parsing the symbolic form
//
// The inverse of number_to_symbolic: reads back the exact expression it
// prints, so an exact value -- irrationals included -- can round-trip through
// text. The grammar is exactly what that printer emits:
//
//   expr   := term ((' + ' | ' - ') term)*
//   term   := unary (('*' | '/') unary)*
//   unary  := '-' unary | power
//   power  := atom ('^' digits)?
//   atom   := '(' expr ')' | digits | 'pi' | fn '(' expr ')'
//   fn     := 'sin' | 'cos' | 'exp' | 'ln' | 'atan' | 'sqrt'
//
// A rational prints as "p/q", which parses as a division of two integers and
// evaluates back to exactly p/q, so it needs no separate rule. Whitespace is
// skipped rather than required, so the spacing the printer chooses per
// operator ("1/2 + sqrt(5)/2") is not something a reader has to reproduce.
//
// Every production builds its result with the ordinary public constructors,
// so the parse is exact by the same rules the original computation was.

typedef struct {
    const char *pos;
    bool failed;
} sym_parser;

static number sym_expr(sym_parser *p);

static void sym_skip_space(sym_parser *p)
{
    while (*p->pos == ' ' || *p->pos == '\t')
        p->pos++;
}

// Consumes `word` only when it isn't a prefix of a longer identifier, so
// "sin" doesn't match inside a hypothetical "sinh".
static bool sym_word(sym_parser *p, const char *word)
{
    size_t n = strlen(word);
    if (strncmp(p->pos, word, n) != 0) return false;
    char after = p->pos[n];
    if (isalnum((unsigned char)after) || after == '_') return false;
    p->pos += n;
    return true;
}

static number sym_fail(sym_parser *p)
{
    p->failed = true;
    return NUMBER_ERROR;
}

static number sym_atom(sym_parser *p)
{
    sym_skip_space(p);

    if (*p->pos == '(') {
        p->pos++;
        number inner = sym_expr(p);
        sym_skip_space(p);
        if (*p->pos != ')') return sym_fail(p);
        p->pos++;
        return inner;
    }

    if (isdigit((unsigned char)*p->pos)) {
        const char *start = p->pos;
        while (isdigit((unsigned char)*p->pos))
            p->pos++;
        size_t len = (size_t)(p->pos - start);
        char *digits = xmalloc(len + 1);
        memcpy(digits, start, len);
        digits[len] = '\0';
        number value = number_from_decimal(digits);
        if (number_is_error(value)) return sym_fail(p);
        return value;
    }

    if (sym_word(p, "pi")) return number_pi();

    static const struct {
        const char *name;
        number (*fn)(number);
    } fns[] = {
        {"sqrt", number_sqrt}, {"sin", number_sin}, {"cos", number_cos},
        {"exp", number_exp},   {"ln", number_ln},   {"atan", number_atan},
    };
    for (size_t i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        if (!sym_word(p, fns[i].name)) continue;
        sym_skip_space(p);
        if (*p->pos != '(') return sym_fail(p);
        p->pos++;
        number arg = sym_expr(p);
        sym_skip_space(p);
        if (*p->pos != ')') return sym_fail(p);
        p->pos++;
        if (p->failed) return NUMBER_ERROR;
        number value = fns[i].fn(arg);
        if (number_is_error(value)) return sym_fail(p);
        return value;
    }

    return sym_fail(p);
}

static number sym_power(sym_parser *p)
{
    number base = sym_atom(p);
    if (p->failed) return NUMBER_ERROR;
    sym_skip_space(p);
    if (*p->pos != '^') return base;
    p->pos++;
    sym_skip_space(p);
    if (!isdigit((unsigned char)*p->pos)) return sym_fail(p);
    // The printer only ever emits a small unsigned exponent (a repeat count
    // from a product chain), so this doesn't need the general atom rule.
    uint64_t exponent = 0;
    while (isdigit((unsigned char)*p->pos)) {
        if (exponent > (UINT64_MAX - 9) / 10) return sym_fail(p);
        exponent = exponent * 10 + (uint64_t)(*p->pos++ - '0');
    }
    number value = number_pow(base, number_from_int((int64_t)exponent));
    if (number_is_error(value)) return sym_fail(p);
    return value;
}

static number sym_unary(sym_parser *p)
{
    sym_skip_space(p);
    if (*p->pos == '-') {
        p->pos++;
        number inner = sym_unary(p);
        return p->failed ? NUMBER_ERROR : number_neg(inner);
    }
    return sym_power(p);
}

static number sym_term(sym_parser *p)
{
    number acc = sym_unary(p);
    for (;;) {
        if (p->failed) return NUMBER_ERROR;
        sym_skip_space(p);
        char op = *p->pos;
        if (op != '*' && op != '/') return acc;
        p->pos++;
        number rhs = sym_unary(p);
        if (p->failed) return NUMBER_ERROR;
        number value = op == '*' ? number_mul(acc, rhs) : number_div(acc, rhs);
        if (number_is_error(value)) return sym_fail(p);
        acc = value;
    }
}

static number sym_expr(sym_parser *p)
{
    number acc = sym_term(p);
    for (;;) {
        if (p->failed) return NUMBER_ERROR;
        sym_skip_space(p);
        char op = *p->pos;
        if (op != '+' && op != '-') return acc;
        p->pos++;
        number rhs = sym_term(p);
        if (p->failed) return NUMBER_ERROR;
        number value = op == '+' ? number_add(acc, rhs) : number_sub(acc, rhs);
        if (number_is_error(value)) return sym_fail(p);
        acc = value;
    }
}

number number_from_symbolic(const char *str)
{
    if (!str) return err(ERR_PARSE);
    sym_parser p = {.pos = str, .failed = false};
    number value = sym_expr(&p);
    if (p.failed) return err(ERR_PARSE);
    sym_skip_space(&p);
    if (*p.pos != '\0') return err(ERR_PARSE); // trailing garbage
    return value;
}

// ---------------------------------------------------------------------------
// Fuzz harness: the checks that still carry independent information now
// that the arithmetic substrate is GMP. Two things survive: the GCD
// cross-check (a plain Euclidean gcd built on division, an algorithm
// genuinely distinct from mpz_gcd's HGCD, over a size sweep dense around
// the word-size boundaries our fits-checks and views sit on), and
// number-level round trips through the public API (construction, string
// conversion, arithmetic identities) that exercise this library's own
// glue: tier demotion, canonicalization, sign handling, parsing/printing.
// The old primitive property tests (divmod identity, mul commutativity,
// shift round trips, isqrt bounds) verified GMP against GMP once the
// backend switched, so they were dropped -- GMP's own test suite owns
// that. Not part of the library build; run via `make fuzz` (builds with
// -DFUZZ_MAIN and ASan/UBSan).
#ifdef FUZZ_MAIN

static long g_total = 0, g_failed = 0;

// Full-width random 64-bit limb (rand() yields only ~31 bits, so stitch
// four draws) -- important so the fuzz checks actually exercise the high
// half of each limb, not just the low 32 bits.
static bi_limb rand_limb(void)
{
    return (bi_limb)(uint32_t)rand() | ((bi_limb)(uint32_t)rand() << 16) |
           ((bi_limb)(uint32_t)rand() << 32) | ((bi_limb)(uint32_t)rand() << 48);
}

static bigint *bi_rand(uint32_t max_limbs)
{
    uint32_t len = max_limbs == 0 ? 0 : 1u + (uint32_t)(rand() % (int)max_limbs);
    bigint *b = bi_new();
    for (uint32_t i = 0; i < len; i++) { // most-significant limb first
        mpz_mul_2exp(b, b, GMP_LIMB_BITS);
        mpz_add_ui(b, b, rand_limb());
    }
    return b;
}

static void bi_print(const char *label, const bigint *b)
{
    char *s = bi_to_decimal(b);
    fprintf(stderr, "%s = %s (limbs=%zu)\n", label, s, mpz_size(b));
}

// Records a check's outcome, printing a,b (and any extra bigints) on
// failure. `extra` is a NULL-terminated array of (label, bigint*) pairs.
static bool report(bool ok, const char *tag, const bigint *a, const bigint *b, ...)
{
    g_total++;
    if (ok) return true;
    g_failed++;
    fprintf(stderr, "MISMATCH (%s):\n", tag);
    bi_print("  a", a);
    if (b) bi_print("  b", b);
    va_list ap;
    va_start(ap, b);
    for (;;) {
        const char *label = va_arg(ap, const char *);
        if (!label) break;
        bi_print(label, va_arg(ap, const bigint *));
    }
    va_end(ap);
    return false;
}

// ---- GCD: cross-checked against a plain Euclidean gcd built on bi_divmod,
// simple enough to trust directly and -- crucially -- an algorithm distinct
// from mpz_gcd's HGCD, so it is a real independent oracle.
static bigint *fuzz_gcd_ref(const bigint *a, const bigint *b)
{
    bigint *x = bi_copy(a), *y = bi_copy(b);
    while (!bi_is_zero(y)) {
        bigint *q, *r;
        bi_divmod(x, y, &q, &r); // x mod y (bi_divmod handles x < y)
        bi_free(q);
        bi_free(x);
        x = y;
        y = r;
    }
    bi_free(y);
    return x; // gcd in x; zero-operand cases fall out correctly (gcd(a,0)=a)
}

static bool gcd_check(const bigint *a, const bigint *b, const char *tag)
{
    bigint *expect = fuzz_gcd_ref(a, b);
    bigint *got = bi_new();
    mpz_gcd(got, a, b);
    bool ok = report(bi_cmp(expect, got) == 0, tag, a, b, "  expect (euclid)", expect, "  got (hybrid)", got,
                      (const char *)NULL);
    bi_free(expect);
    bi_free(got);
    return ok;
}

static void run_gcd_fuzz(void)
{
    for (uint32_t max_limbs = 1; max_limbs <= 80; max_limbs++) {
        for (int trial = 0; trial < 300; trial++) {
            bigint *a = bi_rand(max_limbs);
            bigint *b = bi_rand(max_limbs);
            gcd_check(a, b, "gcd-random");
            bi_free(a);
            bi_free(b);
        }
    }

    // Dense coverage of small limb counts, where word-size boundary bugs
    // would hide.
    for (uint32_t limbs = 1; limbs <= 15; limbs++) {
        for (int trial = 0; trial < 3000; trial++) {
            bigint *a = bi_new(), *b = bi_new();
            for (uint32_t i = 0; i < limbs; i++) {
                mpz_mul_2exp(a, a, GMP_LIMB_BITS);
                mpz_add_ui(a, a, ((uint32_t)rand() << 16) ^ (uint32_t)rand());
                mpz_mul_2exp(b, b, GMP_LIMB_BITS);
                mpz_add_ui(b, b, ((uint32_t)rand() << 16) ^ (uint32_t)rand());
            }
            gcd_check(a, b, "gcd-boundary");
            bi_free(a);
            bi_free(b);
        }
    }

    // Adversarial: consecutive Fibonacci-like numbers are the classic
    // Euclidean-algorithm worst case (every quotient is 1).
    {
        bigint *f0 = bi_from_u64(1), *f1 = bi_from_u64(1);
        for (int i = 0; i < 400; i++) {
            gcd_check(f1, f0, "gcd-fibonacci");
            bigint *next = bi_add(f0, f1);
            bi_free(f0);
            f0 = f1;
            f1 = next;
        }
        bi_free(f0);
        bi_free(f1);
    }

    // Adversarial: equal values, one a multiple of the other, coprime-ish
    // (differ by 1), and powers of two with various offsets.
    for (uint32_t max_limbs = 1; max_limbs <= 60; max_limbs += 2) {
        bigint *a = bi_rand(max_limbs);
        if (bi_is_zero(a)) {
            bi_free(a);
            continue;
        }
        gcd_check(a, a, "gcd-equal");

        bigint *m = bi_mul_u32(a, (uint32_t)(2 + rand() % 50));
        gcd_check(m, a, "gcd-multiple");
        bi_free(m);

        bigint *one = bi_from_u64(1);
        bigint *aplus1 = bi_add(a, one);
        gcd_check(aplus1, a, "gcd-differ-by-1");
        bi_free(one);
        bi_free(aplus1);

        bi_free(a);
    }
    for (uint32_t bits = 1; bits <= 2000; bits += 17) {
        bigint *one = bi_from_u64(1);
        bigint *pa = bi_shl(one, bits);
        bigint *pb = bi_shl(one, bits > 11 ? bits - 11 : 1);
        gcd_check(pa, pb, "gcd-powers-of-two");
        bi_free(one);
        bi_free(pa);
        bi_free(pb);
    }

    // Zero / tiny edge cases.
    {
        bigint *zero = bi_new();
        bigint *five = bi_from_u64(5);
        gcd_check(zero, five, "gcd-zero-a");
        gcd_check(five, zero, "gcd-zero-b");
        bi_free(zero);
        bi_free(five);
    }

    // Large-operand stress test, well beyond anything the benchmarks/
    // workloads actually reach, to catch any scale-dependent issue.
    for (int trial = 0; trial < 200; trial++) {
        bigint *a = bi_rand(600), *b = bi_rand(600);
        gcd_check(a, b, "gcd-large");
        bi_free(a);
        bi_free(b);
    }
}

// ---- number-level round trips through the public API: exercises
// canon_make, number_addsub, number_mul, and number_div together, on top
// of the bigint-layer checks above.

static void run_number_fuzz(void)
{
    for (int trial = 0; trial < 20000; trial++) {
        int64_t an = (int64_t)(rand() - RAND_MAX / 2), ad = 1 + rand() % 100000;
        int64_t bn = (int64_t)(rand() - RAND_MAX / 2), bd = 1 + rand() % 100000;
        number a = number_from_ratio(an, ad);
        number b = number_from_ratio(bn, bd);

        if (!number_is_zero(b)) {
            number q = number_div(a, b);
            number back = number_mul(q, b);
            g_total++;
            if (number_compare(back, a) != 0) {
                g_failed++;
                fprintf(stderr, "MISMATCH (number-div-mul-inverse): a=%lld/%lld b=%lld/%lld\n", (long long)an,
                        (long long)ad, (long long)bn, (long long)bd);
            }
        }

        number sum = number_add(a, b);
        number back2 = number_sub(sum, b);
        g_total++;
        if (number_compare(back2, a) != 0) {
            g_failed++;
            fprintf(stderr, "MISMATCH (number-add-sub-inverse): a=%lld/%lld b=%lld/%lld\n", (long long)an,
                    (long long)ad, (long long)bn, (long long)bd);
        }

    }
}

int main(void)
{
    srand(67890);
    run_gcd_fuzz();
    run_number_fuzz();
    fprintf(stderr, "%ld/%ld passed (%ld failed)\n", g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}

#endif // FUZZ_MAIN


