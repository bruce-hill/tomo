# Add Toom-3 multiplication above the Karatsuba range

`bi_mul` is Karatsuba-only above KARATSUBA_LIMIT (number.c). Karatsuba is
Theta(n^1.585); GMP escalates Karatsuba -> Toom-3 (n^1.465) -> Toom-4 ->
Toom-6h/8h -> FFT. This is the dominant reason the mul gap *grows* with
size rather than staying a constant: measured 6.0x at 4096 bits widening to
18.2x at 524288 bits (bigint_bench). A single Toom-3 tier recovers a large
part of the 1k-50kbit range where most "big rational" work actually lives.

## Plan

- Toom-3 splits each operand into 3 parts, evaluates the product polynomial
  at 5 points (0, 1, -1, 2, inf are the usual choice), does 5 half*-size
  recursive multiplies, then interpolates. The interpolation involves exact
  small-constant divisions (by 2, by 3) -- use bi_divmod_u32, which is
  exact here.
- Recurse through the same bi_mul dispatch: schoolbook < KARATSUBA_LIMIT <
  Karatsuba < TOOM3_LIMIT < Toom-3. Karatsuba stays as the middle tier.
- Signed intermediates: the evaluation points produce values that can be
  negative (the -1 point), so this needs a sign-tagged temporary or the
  bi_combine-style abs+sign handling already in the gcd code.
- Watch allocation: Toom-3 has more temporaries than Karatsuba. Pairs well
  with scratch-buffer-alloc.md; do that first or together if the malloc
  count regresses.

## Verify

- `make fuzz` -- the property tests multiply random operands across a wide
  size range and cross-check; extend the size range past TOOM3_LIMIT.
- `make bigint_bench`; tune TOOM3_LIMIT (GMP's MUL_TOOM33_THRESHOLD is
  ~100-ish 64-bit limbs as a starting reference, but measure here).

## Depends on / pairs with

Best done *after* 64-bit-limbs.md (fewer limbs, thresholds shift) and
alongside scratch-buffer-alloc.md (Toom-3's temporary count). Toom-4 and
FFT are a separate, later step -- see fft-mul-hgcd.md.
