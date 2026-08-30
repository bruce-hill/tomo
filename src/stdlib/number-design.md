# Numbers

`number` is a C library for a general-purpose numeric datatype for doing
numeric calculations with the following properties:

- All `number`s represent an **exact** computable real number, including
  irrational numbers.
- `number`s can be converted to a textual representation with an arbitrary
  number of exact digits, and to a correctly rounded IEEE754 floating point
  number.
- Common rational arithmetic stays off the heap entirely for performance.

This library is built on the work of Hans J. Boehm's ["Towards an API for the
Real Numbers"](https://gwern.net/doc/math/2020-boehm.pdf) and his [Constructive
Reals Java library](https://hboehm.info/crcalc/CRCalc.html) (CRCalc) built for
the Android calculator app.

Boehm has two implementations, and this library follows the pattern of the
second one:

- **CRCalc** is the pure constructive-real engine: every number is a lazy
  expression node with a memoized best-known approximation. It is fully
  general, but every operation allocates.
- **UnifiedReal** (the Android calculator's numeric type, described in the
  2020 paper) is a hybrid: a number is a *rational* multiplied by a
  *constructive real factor* that is tagged with a known symbolic form (1, π,
  √n, ln n, ...). Rational-only arithmetic never touches the constructive real
  machinery, and the symbolic tags make identities like
  `sqrt(2) * sqrt(2) == 2` and many comparisons decidable, whereas pure
  constructive real evaluation alone can only ever prove a result is *within*
  2⁻ⁿ of 2, never that it *is* 2.

This library is a C implementation of the UnifiedReal pattern, with an
additional immediate (non-heap) representation beneath it for small rationals.

## Data Representation

A `number` is a 64-bit value in one of three tiers:

1. **Small rational**: an immediate value, no heap allocation, holding a 32-bit
   signed numerator, a 30-bit unsigned denominator, and a 2-bit tag.
2. **Big rational**: a pointer to a heap-allocated canonical `mpq_t`.
3. **Real**: a pointer to a heap-allocated structure representing
   `rational × crFactor`, where the rational part is exact and the factor is a
   constructive real tagged with its symbolic form when known.

### Tagging

The low 2 bits of the 64-bit value distinguish the tiers:

- `00`: heap pointer, used as-is with no masking (heap objects are 8-byte
  aligned, so real pointers always have low bits `000`). A header field in the
  heap object distinguishes big rationals from reals.
- `01`: small rational, with a signed numerator in the high 32 bits and an
  unsigned denominator in bits 2–31.
- `11`: the error value. The upper 62 bits carry a reason code (division by
  zero, square root of a negative number, ...) into a static message table;
  `NUMBER_ERROR` is code 0 ("undefined result", no specific reason given).
  Since only the low 2 bits are checked, an error value never needs a heap
  allocation or refcounting no matter which reason it carries, since retain/drop
  already treat every non-pointer tag as a no-op.

The remaining tag value (`10`) is reserved. Note that the all-zeros word is a
NULL pointer, not a valid number; rational zero is encoded as 0/1 with tag
`01`.

### Tier 1: Small rationals

Small rationals are kept **canonical**: gcd(numerator, denominator) = 1,
denominator > 0, sign carried by the numerator. Canonical form means equality
of small rationals is bit-equality, and makes overflow checks meaningful
(a result is promoted only if it doesn't fit *after* reduction).

Arithmetic is done in 64-bit intermediates (32×32→64 multiplies, or
`__builtin_mul_overflow`), followed by gcd reduction. If the reduced result
fits in 32/30 bits, it is re-packed as a small rational; otherwise it is
promoted to a big rational.

**Integer sub-path**: when both operands have denominator 1, the most common
case in practice, the gcd reduction is skipped entirely: addition,
subtraction, and multiplication are a single overflow-checked integer
operation plus re-tagging. This one-branch check makes small-integer
arithmetic cost a couple of cycles, comparable to hardware floating point but
exact.

### Tier 2: Big rationals

A heap-allocated canonical `mpq_t` (GMP rational) with the same canonical
form and the same semantics, just without size limits. Every operation that
produces a big rational checks whether the result fits in a small rational and
**demotes** it if so, since long computations tend to re-shrink and demotion is
what keeps the fast path hot.

### Tier 3: Reals

> **Implementation status**: the current implementation stores reals as the
> *linear* form `a + b·F` (a, b rational; F = π, √n for a non-square integer
> n, ln(r) for a rational r > 1, or exp(r) for any rational r) rather than
> the pure product form below. The linear form keeps addition closed within
> each factor class: each `√n` class forms the field Q(√n) (closed under
> + − × ÷), π forms are closed under affine operations, and LN/EXP forms are
> closed under same-argument affine combination plus two narrower
> identities: `exp(a)·exp(b) = exp(a+b)` (and its inverse) for the pure
> `b·EXP(a)` form, and `b1·ln(r1) + b2·ln(r2) = ln(r1^b1·r2^b2)` when both
> coefficients are integers (so e.g. `2·ln(2) − ln(4)` collapses to exactly
> `0`, not an unresolved difference of two LN terms). `exp(ln(r)) == r` and
> `ln(exp(r)) == r` are recognized exactly, with no interval refinement.
> Commensurable radicands are unified via a perfect-square check (`√8 →
> 2√2`), which also makes equality and comparison exact and total.  This
> symbolic form is tried first for every operation; results outside every
> closed form (π+√2, π², 1/π, ln(2)+ln(3)·(1/2), exp(2)+exp(3), ...) fall
> back to the general `IRRATIONAL` CR machinery described below, which is
> implemented. sin, cos, tan, asin, acos, atan, exp, ln, log10, sinh, cosh,
> tanh, and pow are all built on six CR primitives (SIN, COS, EXP, LN, ATAN,
> SQRT) plus generic ADD/SUB/MUL/DIV nodes for combining values that don't
> unify symbolically. `sqrt` of an already-irrational value (nested roots
> like `sqrt(sqrt(2))`, or `sqrt(pi)`) goes through the SQRT primitive the
> same way; asin/acos inherit this since they're built directly on `sqrt`.
> Cross-tag known-unequal rules (e.g. an EXP/LN factor is never a rational
> multiple of a SQRT factor, since one is transcendental and the other
> algebraic) are *not* implemented beyond what falls out of numeric interval
> refinement, so a rational multiple of π is deliberately left undecided
> against EXP/LN (whether e.g. e is a rational multiple of π is an open
> problem in general, so no such rule would be sound to assert here). The
> hardware floating-point fast path below is implemented for sign
> determination / comparison and few-digit decimal output (see the
> revised scope in that section); `number_to_double` deliberately stays on
> the bigint path (a same-precision double interval cannot decide a
> correctly rounded 53-bit result, which would take double-double interval
> arithmetic, which remains future work).

A real is stored in factored form, following UnifiedReal:

    value = rational × crFactor

where the rational part reuses the tier 1/2 representation and `crFactor` is a
constructive real annotated with a symbolic form tag:

- `ONE` (the factor is exactly 1, so the number is actually rational)
- `PI`
- `SQRT(r)` for rational r
- `LN(r)`, `EXP(r)` for rational r
- `IRRATIONAL`: an arbitrary constructive real expression with no known form

The symbolic tags enable exact simplification and decidable comparison in the
common cases: multiplying two `SQRT` factors with equal radicands yields a
rational; `SQRT(r)` of a perfect-square rational never creates a real at all;
two numbers whose factors carry the same tag compare by comparing their
rational parts; numbers with *different* known-irrational tags are known
unequal. When an operation produces a value outside the closed set of forms
(e.g. adding two numbers with different irrational factors), the result falls
back to an `IRRATIONAL` factor built from the general CR machinery.

The constructive real itself follows CRCalc: a lazy DAG (operations share
subexpressions, so it is a DAG, not a tree) of arithmetic operations and
transcendental functions. Each node memoizes its best-known approximation as a
(precision, value) pair; a request for precision the node has already met is
served from the cache, and a request for more precision recomputes and updates
the cache. Approximations are computed by interval/Taylor-series methods
exactly as in CRCalc.

**Hardware floating-point fast path**: before scaled-bigint evaluation, a
request that a low-precision answer can decide is first attempted with
hardware `double` interval arithmetic, a recursive descent over the same
value structure, each node computing a [lower, upper] bound pair widened
outward by enough ulps to be rigorous (1 ulp for IEEE's correctly rounded
`+ - * /`/`sqrt`; a generous 8 for libm's `exp`/`ln`/`atan`/`sin`/`cos`,
whose documented worst-case errors are 1–2 ulp; `sin`/`cos` are bounded by
Lipschitz continuity around the interval midpoint, so any argument width is
handled). If the interval decides the question, whether a sign/comparison or a
k-digit decimal rounding, the answer is served at hardware speed and is
still exact; if the interval is too wide (cancellation, overflow, a shared
DAG deep enough to exhaust the descent's visit budget), evaluation falls
back to the bigint path, so a failed attempt costs nanoseconds and can
never produce a wrong answer. Measured (see `benchmarks/filter_bench.c`),
this is ~10x on comparison and few-digit output of calculator-style
expressions. Two boundaries drawn deliberately: `number_to_double` is *not*
served (deciding a correctly rounded 53-bit result means separating values
half an ulp apart, which is impossible for a same-precision interval;
double-double interval arithmetic could lift this and is future work), and
building with
`-DNUMBER_NO_DOUBLE_FILTER` removes the fast path entirely for anyone
unwilling to rely on their libm staying within the widening margin.

### Operation dispatch

Every arithmetic operation checks tiers in order:

1. Both operands small rational → inline 64-bit arithmetic, no allocation.
2. Both operands rational (either size) → when both numerator and denominator
   fit 64 bits (the common case even for heap bigrats), the whole op runs in
   u64/u128 with GMP-style reduction (cross-cancel the coprime diagonals, or
   reduce denominators first for add/sub) and demotes if the result fits,
   with no `mpq` allocation. Otherwise it falls back to `mpq` arithmetic.
   This keeps rational arithmetic competitive with GMP directly.
3. At least one real → symbolic-form rules first, general CR construction only
   as a last resort.

Irrational-*producing* functions (`sqrt`, `sin`, `exp`, `log`, `pow`, ...)
likewise check rational escapes before allocating: `sqrt` of a perfect square,
`x^n` for integer n, `sin(0)`, `ln(1)`, etc.

## Memory Management

Numbers have value semantics from the API's perspective, but tiers 2 and 3 are
heap-backed, so heap objects are **reference counted**. Copying a number
increments a refcount; `number_drop` decrements and frees at zero. CR nodes
hold references to their operand nodes, so dropping the root of an expression
DAG releases the whole structure.

Two things Boehm gets from the JVM must be replaced explicitly:

- **Garbage collection** → reference counting, as above. The DAG structure is
  acyclic by construction, so refcounting is sound (no cycle collector
  needed).
- **`synchronized` memoization** → the memoized approximation in a CR node is
  interior-mutable state on a shared object. Initially the library is
  single-threaded-per-number (sharing a number across threads without external
  synchronization is undefined); if thread safety is needed later, the cache
  update is a small critical section suitable for a per-node lock or an
  atomic compare-and-swap of a cache struct.

Everything about a number except the approximation cache is immutable after
construction.

## Precision, Comparison, and Conversion

Internal precision is **binary**: an approximation request at precision `n`
returns a value within 2⁻ⁿ of the true value (matching CRCalc). Decimal digit
counts for textual output are converted to binary precision internally.

- **Comparison**: exact comparison of computable reals is undecidable in
  general, so the comparison API is `number_compare(x, y, precision)`,
  returning −1, +1, or 0 meaning "indistinguishable at this precision", where a
  0 is *not* a proof of equality. However, when both operands have known
  symbolic forms (including plain rationals), comparison is exact and the
  precision bound is not consulted. The common cases are decidable; only
  genuinely `IRRATIONAL`-tagged comparisons are approximate.
  Approximate comparisons use a **floating-point filter** (the same pattern
  computational geometry libraries like CGAL use): a first pass evaluates both
  operands as hardware `double` intervals, and if the intervals don't overlap
  the comparison is decided in nanoseconds. Only near-ties fall through to
  precise evaluation at increasing precision up to the caller's bound.
- **Textual output**: digits are generated exactly, so a number prints as `2`,
  not `2.0000000000`, whenever its rational/symbolic representation proves it.
  For `IRRATIONAL` values, output to k digits is correctly rounded by
  computing at increasing precision until the k-digit rounding is decided.
- **IEEE754 conversion**: `number_to_double` is correctly rounded, using the
  same increase-precision-until-rounding-is-decided loop. Conversion *from* a
  double is exact (every finite double is a rational).

## Undefined Operations

Operations with no defined result (`1/0`, `sqrt(-1)`, `ln(0)`) return a
distinguished error value that propagates through arithmetic (any operation on
an error yields an error), similar to NaN but detectable via `number_is_error`.
Unlike NaN, the error value isn't a single bit pattern: `number_error_message`
reports *why* (`"division by zero"`, `"square root of a negative number"`,
...) via a reason code packed into the value's own bits (see "Tagging"
above), at no cost, since the error value stays a tagless immediate no matter
which reason it carries. When two already-erroneous values combine (e.g.
`2/0 + sqrt(-1)`), the result carries whichever operand's reason the
implementation happened to check first; propagation always keeps the
original reason rather than resetting to a generic one, so the message
surfacing from a large expression is the *original* failure, not a
description of whatever operation last touched the error on its way out.
One case is undetectable in general: division by an `IRRATIONAL` value that
happens to equal zero cannot be diagnosed at construction time and will
instead fail to converge when an approximation is demanded; approximation
requests therefore take a precision bound beyond which they give up and
produce an error.

## Known Limitation: Expression Growth

The factored representation means purely rational computation, the
overwhelmingly common case, never allocates a CR node, no matter how long the
computation runs. But genuinely irrational accumulation (e.g.
`x = x + sin(1)` in a loop) still builds a chain of CR nodes whose depth grows
with the iteration count, with memory and re-evaluation cost to match. This is
inherent to exact real arithmetic; the library does not attempt to bound it.
Callers who need bounded cost for irrational accumulation should round to a
rational at chosen points (`number_round`, which is exact and cheap) and
accept the explicit loss of exactness.

## Rejected Alternative: Optimistic Floating-Point Representation

A fourth representation tier, storing values as hardware doubles, detecting
inexact operations (via error-free transformations like TwoSum/FMA residuals),
and falling back to rationals, was considered and rejected:

- A double only represents *dyadic* rationals (n/2ᵏ). Integers and
  halves/quarters are exact, but `0.1`, `0.3`, and `1/3` are not, so the most
  common non-integer values would fall through to the rational path anyway
  after paying for a failed floating-point attempt.
- A full double needs all 64 bits, which conflicts with the 2-bit tag. The fix
  (NaN-boxing) leaves only ~51 payload bits, shrinking small rationals to
  ~25/25 bits and creating a canonical-form problem (5 as double `5.0` vs.
  rational `5/1`) that breaks bit-equality without per-operation
  canonicalization checks.
- The main workload it would accelerate, small-integer arithmetic, is
  already covered by the tier 1 integer sub-path at comparable cost.

The optimistic-float idea instead lives where the fallback is genuinely
expensive: the hardware interval fast path in the approximation engine and the
floating-point comparison filter, both described above.

## Open Questions

- The exact closed set of symbolic forms: UnifiedReal has grown forms over
  time (e.g. `SQRT` handling of rational multiples inside the radical);
  `PI`/`SQRT`/`LN`/`EXP` are implemented now (see the "Implementation
  status" note above for exactly which combinations stay closed). Expand
  further as simplification opportunities are identified.
- Whether the rational part of a real should itself be allowed to be a small
  rational immediate (saving an allocation per real) or always a bigint pair
  (simpler code). Leaning toward reusing the full tiered representation.
- Bigint backend: **GMP types throughout** (this branch drops the custom
  bigint entirely). A heap big rational holds a canonical `mpq_t` (sign in
  the numerator); tier-2 arithmetic/comparison is direct `mpq_*` calls on
  zero-copy views (small immediates are viewed via `mpz_roinit_n` over
  stack limbs). Tier 3's magnitudes are heap `mpz_t`s behind a thin
  ownership veneer (`bi_*`, one-line wrappers that keep the engine's
  sign+magnitude value style), and the hot series loops run on in-place
  `mpz_t` locals that reuse their buffers across iterations. The public API
  is identical. This brings an LGPL link dependency on `-lgmp`, and GMP's
  limb buffers follow `mp_set_memory_functions` rather than
  `number_set_allocator` (see number.h); the zero-dependency
  vendor-two-files build lives on the `main` branch.
