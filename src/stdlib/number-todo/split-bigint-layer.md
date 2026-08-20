# Consider splitting the bigint layer out of number.c

number.c is ~5,100 lines. The bigint layer (bi_add/bi_sub/bi_mul/bi_divmod/
bi_divmod_knuth/bi_gcd/bi_gcd_lehmer/bi_isqrt/bi_shl/bi_shr/...) is already
a self-contained internal API with its own fuzz harness (FUZZ_MAIN) and
benchmark harness (BIGINT_BENCH_MAIN), so it has a natural seam: an
internal number-bigint.h/number-bigint.c that number.c consumes.

Benefits: navigability, and the fuzz/bench harnesses get their own
translation units instead of -D blocks at the end of number.c.

Costs / reasons to skip: single-file distribution is a genuine virtue for a
vendor-these-two-files library, and the -D harness blocks currently get to
test static functions directly. Only worth doing if distribution ever moves
to a built library anyway; otherwise closing this as "won't do" is a fine
outcome.
