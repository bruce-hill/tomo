# What a `-` turns into: part of a numeric literal, a negation, or subtraction,
# and how far its operand reaches. Only the parse tree tells these apart; the
# values they produce are checked in test/operators.tm.
func main()
    # Written against the digits, the sign belongs to the literal; written
    # apart from them, it's a negation of one. Both denote the same value.
    >> -2
    >> - 2
    >> -(2)
    >> - -2
    >> --2
    >> -.5
    >> -0x10
    >> -1_000
    >> -1e3
    >> -50%
    >> -30deg

    # Subtraction, whatever the spacing around the operator:
    >> a - b
    >> a-b
    >> a- b
    >> a - -b
    >> a--b
    >> a - -1

    # Negation binds looser than `^`, tighter than everything else:
    >> -x^2
    >> -2^2
    >> -x*2
    >> -x+2
    >> -x mod 3
    >> -1 < 0
    >> 2^-x
    >> -x^-y^-z
    >> x _min_ -1

    # A term's own suffixes bind tighter than the negation in front of it:
    >> -x.abs()
    >> -2.abs()
    >> -xs[1]
    >> -x!
    >> -12..round()

    # Other prefixes nest either way round:
    >> @-1
    >> &-1
    >> - -x
    >> not -x
    >> -not x
