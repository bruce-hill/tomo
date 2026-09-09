# The spaces around a binary operator say where an expression splits, so they
# are kept -- except in the `*` band of an expression that also holds a `+`
# band operator, where dropping them is what shows the split. `^` never takes
# them, and a subscript takes none at all: the brackets already bound it.

func foo(v:Int -> Int)
    return v

func main()
    x := 1
    y := 2
    z := 3
    r := 2.0
    arr := [1, 2, 3]

    # One band, so the spaces stay:
    >> x + y
    >> x * y
    >> 100 / 10 / 2
    >> x == y * z

    # Both, so the tighter one gives them up:
    >> x + y*z
    >> foo(x + y*z)
    >> x*(y + z)//2 + x + 1

    # A subscript is written compactly, whatever it holds:
    >> arr[x+1]
    >> arr[x+y*z]
    # ...though a call inside one is still a call:
    >> arr[foo(x + y)]
    >> arr[(x + y)*z]

    # An exponent sits against what it raises:
    >> x^2
    >> Float64.PI * r^2
    >> x + y^2
    >> 2^3^2

    # Parentheses start a fresh expression with its own answer:
    >> (x + y) * z
    >> x * (y + z)

    # An operator that is a word keeps its spaces wherever it stands, or it
    # stops being a word: `aandb` is one identifier, and `(x + y)mod3` does not
    # parse. A subscript, which drops them from everything else, is the place
    # that would go wrong first.
    >> arr[x and y]
    >> arr[x or y]
    >> arr[x xor y]
    >> arr[x mod y]
    >> arr[x mod1 y]
    >> arr[x _min_ y]
    >> arr[x _max_ y]
    >> arr[x*y mod 3]
    # A parenthesized operand settles its own spacing rather than inheriting
    # the subscript's, so the tightening stops at the parenthesis:
    >> arr[x+y and y]
    >> x + y mod 3
    >> (x and y)^2

    # `!=` keeps them for the same reason: `a!=b` reads as the `!` suffix on
    # `a`, followed by `= b`.
    >> arr[x != y]
    >> arr[x+y != y]
