# Sub-expressions in statement positions, where something follows them: an
# `assert`'s message, a set's `,`, a suffix, an interpolation's closing paren.
# A bare `if`/`match` would run on through all of them.

enum E(A{q:Int})

func main()
    a := yes
    b := no
    x := 1
    y := 2
    e := E.A{1}
    # The message has to land after the assertion, not inside it:
    assert (if yes then x else y) > 0, "message"
    assert (match e case A{q} then q else 0) > 0, "message"
    # A set element is bounded like a list item once it has to break lines
    # (inline, the `,` already ends it):
    >> {(if yes then x else y), 3}
    >> {(if yes then x else y + 111111111111111111111 + 22222222222222222222 + 3333333333333333333 + 444444444444444)}
    # A lambda takes a suffix only in parentheses: `func() x.abs()` is the
    # lambda of `x.abs()`, not the lambda's `.abs()`.
    >> (func() x).abs()
    # An interpolation is one line whatever it holds, because a newline inside
    # a text literal is part of the text:
    say("pre $(if yes then x else y + 1111111111111111 + 22222222222222 + 3333333333333) post")
