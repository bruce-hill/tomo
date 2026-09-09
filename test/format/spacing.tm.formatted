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
