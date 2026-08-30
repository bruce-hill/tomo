# `0x` with no digits after it: the parser reports this now that it converts
# the digits to a value itself, rather than a later stage failing on the text.
func main()
    >> 0x
