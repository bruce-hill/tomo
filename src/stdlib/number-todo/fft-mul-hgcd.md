# Very-large-operand tier: FFT multiply and subquadratic (HGCD) gcd

The last, highest-effort tier -- only worth it for callers who work with
operands past ~100 kbit. Below that, 64-bit-limbs + Toom-3 +
divide-and-conquer division close most of the gap; this is what remains at
the extreme end of the bigint_bench sweep.

## FFT / Schoenhage-Strassen multiply

Above Toom-8h GMP uses an FFT-based multiply (mpn_mul_fft), Theta(n log n).
This library tops out at Karatsuba, so at 524288 bits mul is 18.2x GMP and
still widening -- that divergence is n^1.585 vs n log n. An FFT tier (either
Schoenhage-Strassen over Z/(2^k+1), or NTT) is the only thing that flattens
it. Large, self-contained, and gated behind a high threshold, so it is
purely additive to the dispatch in bi_mul.

## Subquadratic gcd (HGCD / half-gcd)

`bi_gcd_lehmer` (number.c) is Lehmer's -- still Theta(n^2) in the large.
GMP uses HGCD (half-gcd), Theta(n log^2 n * ...) built on fast multiply.
gcd is measured 3.7x at 4096 bits widening to 20.2x at 524288 (bigint_bench
gcd) -- the same widening-race signature as mul. HGCD is the fix and it
reuses the fast multiply from the FFT/Toom work, so it belongs in the same
tier of effort.

Note gcd matters here beyond standalone use: every tier-2 rational result
is gcd-reduced to canonical form (canon_make), so big-rational add/mul pay
a gcd. Faster gcd speeds the whole rational layer.

## Verify

- `make fuzz` -- extend the mul/gcd size sweeps well past the FFT/HGCD
  thresholds.
- `make bigint_bench` (a wider sweep than the default may be needed to see
  the crossover cleanly; the divmod/gcd Newton/HGCD thresholds are already
  documented as needing a wider-than-default sweep to observe).

## Depends on

Everything else first. FFT multiply is the foundation both this and
divide-and-conquer division / HGCD build on, so if this tier is ever
attempted, FFT mul is the first piece.
