# Where comments end up. A comment at the end of the last line of a file used
# to be written out twice on the second pass, because a spoofed file counted
# its NUL terminator as a byte and the end-of-file comment scan ran past the
# end. The ones a list or table writes between its elements used to be
# appended where the last element stopped, on the back of its comma. The ones
# written among a call's arguments were dropped outright: an argument carried
# no span and no comment for them to be recorded in.

func f(a:Int -> Int)
    return a

func g(
    # above a parameter
    a:Int,
    b:Int, # after the last, before the close
)
    return a + b

func containers()
    xs := [
        # opening
        1,
        # trailing
        # on its own
        2,
        # left over at the end
    ]
    t := {
        1: 2,
        # trailing
        # left over at the end
    }
    return xs.length + t.length

func calls()
    >> g(
        # before the first
        1,
        # between the arguments
        2, # after the last, before the close
    )
    # A lone argument hugs the delimiters, but not when it carries a comment:
    # only the multi-line form writes those out.
    >> f(
        1, # explaining the argument
    )

func main()
    >> containers()
    >> calls() # the last line of the file

func doubled()
    # A comment on the line a statement ends on belongs to the innermost block
    # that ends there. Claimed by the enclosing one as well, it was written
    # twice per pass, so `tomo format -i` run twice over this file used to
    # leave four copies of the comment below, and eight after a third.
    >> 1 # on the last line of a body

func after_a_block()
    # ...but a statement whose own last line is at its level, like the `]`
    # closing this list, keeps the comment written after it.
    xs := [
        11111111111111111, 22222222222222222, 33333333333333333, 44444444444444444, 555555555555,
        66666666666666, 7777777777777,
    ] # after the list
    return xs.length

enum E(A{q:Int}, B)

func gaps(e:E)
    # A comment written in a gap between the parts of one statement -- before
    # an `else`, after a `match` subject, before a `case` -- belongs to no
    # block, so nothing used to write it out. It takes a line of its own at the
    # statement's level.
    if yes
        say("a")
    # before else
    else
        say("b")

    match e
    # after the subject and before the first case
    case A{q}
        say("$q")
    # before a later case
    case B
        say("b")
    # before the else
    else
        say("other")

func inside_expressions()
    xs := [1, 2, 3]
    # A comment written inside an expression stays where it was written. It can
    # only follow an operator, never precede one: `a # c` and then `+ b` on the
    # next line is not an expression the parser puts back together.
    v := 1 + # after the operator
        2
    w := (
        # after the open paren
        1 + 2
    )
    y := (
        1 + 2 # before the close paren
    )
    z := [
        (x # between the expression and its `for`
        for x in xs),
    ]
    return v + w + y + z.length
