# Layout choices the formatter makes for text literals. `tomo format --verify`
# compares syntax trees, and every layout below parses to the same tree as a
# one-line literal holding the same text, so it can't see the difference
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
    # own, so it is rejoined when it fits on one line:
    rejoined := "this is a long line
    .... that was split in code"
    # ...and becomes a block when it doesn't, so that its text starts at a
    # column of its own rather than wherever the quote happened to land:
    long := "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    # Text too wide for the page is continued with dots filling an indent's
    # width, so it lines up with the line above. A break is never placed right
    # before a dot, which the `..` marker would swallow:
    dotted := "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB........BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
    # A run of dots long enough to need breaking can't stay bare: the marker
    # would swallow the ones it landed in front of. The first dot of a
    # continued line is written escaped so that it survives:
    all_dots := "......................................................................................................................................................"
    # An interpolation is written as one piece, so a break lands beside it,
    # never inside:
    interpolated := "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB$(n + 1)BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
    assert n > 0 and lines.length == 2
    assert block != joined and blank_first != rejoined
    assert long != dotted and dotted != interpolated
    assert all_dots.length == 150

func wrapped_spaces()
    # A space at the end of a written line is part of the text and invisible on
    # the page, so anything that trims trailing whitespace -- an editor on save,
    # a lint, a patch tool -- would silently shorten the value. Written as an
    # escape it survives being read back.
    padded := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa     bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    >> padded.length

func colorized_text(n:Int)
    # `~colorized` renders an interpolated value in the colour its type is
    # shown in, so dropping it changes what the program prints. It used to be
    # dropped, and `--verify` called that faithful: the flag was not in the
    # s-expression the two parse trees were compared as.
    say("the value is $n"~colorized)
    say($HTML"<p>$n</p>"~colorized)
