# How numeric literals and the `-` in front of them are written back out.
# `tomo format --check` compares syntax trees, and `- 2`, `-(2)` and `- (2)`
# all parse to the same one, so it can't see which of them the formatter picks.
# This snapshot pins that choice.

func main()
    # Bases, separators and exponents survive as written:
    >> 0xff
    >> 0o17
    >> 1_000_000
    >> 1e10
    >> 2.0
    >> -.5
    >> 50%
    >> 30deg

    # A negative literal is one token; a negation of a literal is two, and
    # always parenthesizes its operand so the two stay apart:
    >> -2
    >> - 2
    >> - -2
    >> - - 2
    >> -0x10

    # Parentheses a suffix depends on: `-2.abs()` negates `2.abs()`, so the
    # parentheses around the literal have to stay. `-0` is written negative
    # without being valued negative, and needs them just the same.
    >> (-2).abs()
    >> -2.abs()
    >> (-0).abs()
    >> (-0.0).abs()
