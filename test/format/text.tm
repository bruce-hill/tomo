# Layout choices the formatter makes for text literals. `tomo format --verify`
# compares syntax trees, and the two multi-line layouts below parse to the same
# tree as each other and as a one-line literal, so it can't see the difference
# between them. The snapshot beside this file pins the layout itself.

func main()
    # A block -- opening quote alone on its line -- stays a block, because its
    # line breaks are part of the text:
    block := "
        one
        two
    "
    # ...including as the term a suffix hangs off of, which is where squeezing
    # it onto one line would put a whole paragraph past the line limit:
    n := "
        one
        two
    ".length
    # ...as one operand of an operator:
    joined := "
        one
    " ++ "two"
    # ...as one argument among several:
    say("
        one
        two
    ", newline=no)
    # ...and as a list item:
    lines := [
        "
            one
        ",
        "two",
    ]
    # A blank line after the opening quote is a blank line in the text:
    blank_first := "

        after a blank line
    "
    # A line split -- text starting right after the quote, continued by two or
    # more dots at the literal's own indentation -- has no line breaks of its
    # own, so it is rejoined when it fits:
    rejoined := "this is a long line
    .... that was split in code"
    # ...and split again when it doesn't. The dots fill an indent's width, so
    # the continued text lines up with where a block's text would sit:
    long := "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    # A break is never placed right before a dot, which the `..` marker would
    # swallow:
    dotted := "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB........BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
    # A long line inside a block is continued the same way:
    block_split := "
        one
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
    "
    # An interpolation is written as one piece, so a break lands beside it,
    # never inside:
    interpolated := "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB$(n + 1)BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
    assert n > 0 and lines.length == 2
    assert block != joined and blank_first != rejoined
    assert long != dotted and block_split != interpolated
