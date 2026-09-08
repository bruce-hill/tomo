# Where a sub-expression needs parentheses to stay one. An inline `if`/`match`
# runs on through whatever follows it, and `_min_`/`_max_` are operators like
# any other, so both have to give the syntax around them somewhere to resume.

enum E(A{q:Int})

struct P{x:Int, y:Int}

func id(v:Int -> Int)
    return v

func chosen(c:Bool -> Int)
    # `return if ...` would be read as the postfix `return x if c`:
    return (if c then 1 else 2)

func main()
    a := yes
    b := no
    x := 1
    y := 2
    e := E.A{1}
    # An argument list has to find its comma and its closing paren again:
    >> id((if yes then x else y))
    >> id(v=(match e case A{q} then q else 0))
    >> P{(if yes then x else y), 2}
    >> x.plus((if yes then x else y))
    # ...and a table entry its `:`:
    >> {(if yes then x else y): 1}
    >> {1: (if yes then x else y)}
    # `_min_` and `_max_` group by the same tightness as any other operator:
    >> (a and b) _min_ b
    >> a _min_ (b and a)
    >> (x == y) _min_ x
    >> (if yes then x else y) _min_ x
    >> x _min_ (y _min_ x)
    # ...so an operand it does absorb keeps no parentheses:
    >> x*y _min_ x
    >> x _min_ y _max_ x
