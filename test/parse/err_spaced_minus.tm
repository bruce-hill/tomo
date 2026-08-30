# A space before `-` but not after reads as a negated argument, not
# subtraction (see match_binary_operator), leaving nothing to apply it to.
func main()
    >> a -b
