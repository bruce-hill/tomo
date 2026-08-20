# Divide-and-conquer division + a precomputed limb reciprocal

Division is the worst gap measured: 6.5x at 4096 bits widening to 63.2x at
524288 bits (bigint_bench divmod). Two compounding causes, both fixable:

1. **Basecase is Knuth Algorithm D** (bi_divmod_knuth, number.c), a
   Theta(n*m) schoolbook long division used all the way up to the 12288-limb
   Newton threshold (NEWTON_LIMIT). GMP switches to *recursive*
   divide-and-conquer division (mpn_dc_div_qr, built on fast multiply)
   around ~40 limbs, so its division inherits Toom/FFT speed. This library's
   division stays quadratic until enormous sizes.

2. **The quotient-digit estimate does a real hardware `divide` per quotient
   limb** (`top2 / v[dlen-1]`, bi_divmod_knuth). Division is high-latency and
   unpipelined. GMP computes a reciprocal *once* (`invert_limb` /
   `invert_pi1`) and estimates each digit with a *multiply*.

## Plan

- **Precomputed reciprocal (cheap, do first):** after normalizing the
  divisor (the top limb already has its high bit set), compute a 2/1 or 3/2
  limb reciprocal once and replace the per-limb divide with a multiply-based
  estimate (Moeller-Granlund "Improved division by invariant integers" is
  the reference GMP uses). Big constant-factor win on the existing basecase,
  small code change, no new algorithm.

- **Divide-and-conquer (the asymptotic win):** recursive division that
  splits the quotient in half and uses bi_mul for the multiply-back steps,
  so division cost tracks multiply cost. Lower DC_DIV_THRESHOLD far below
  the current 12288 (GMP's is ~40-50 limbs). The existing bi_divmod_newton
  path (Newton reciprocal, NEWTON_LIMIT=12288) is the very-large tier; DC
  fills the huge gap between Knuth and Newton.

## Verify

- `make fuzz` (property-tests bi_divmod / bi_divmod_knuth against a
  reference across sizes) -- primary safety net.
- `make bigint_bench`; the divmod column is the scoreboard. Re-tune
  DC_DIV_THRESHOLD and NEWTON_LIMIT.

## Depends on

64-bit-limbs.md first (the reciprocal wants a clean 128/64 divide anyway).
The reciprocal step and 64-bit-limbs.md's Knuth quotient-estimate note are
the same piece of code -- do them together.
