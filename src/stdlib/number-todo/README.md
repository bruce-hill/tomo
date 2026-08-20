# Future work on `number`

Notes on known limitations of `number.c`, carried over from the work that
produced it. Each file states a problem, what it would take to fix, and
whether it's worth doing -- none are bugs, and none block correctness.

The performance ones (`toom3-multiplication`, `divide-and-conquer-division`,
`fft-mul-hgcd`) only matter for very large operands; `split-bigint-layer` is
organizational; the rest are API/documentation questions.
