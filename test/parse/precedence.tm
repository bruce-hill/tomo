# Operator precedence and associativity, which nothing else in the test suite
# pins down: the parse tree is the only place these are observable.
func main()
    >> 1 + 2 * 3 - 4
    >> 2 ^ 3 ^ 2
    >> 10 - 3 - 2
    >> 1 + 2 < 3 * 4
    >> not yes and no or yes
    >> -2 ^ 2
    >> (1 + 2) * 3
