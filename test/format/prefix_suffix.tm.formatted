# `@` and `&` bind every suffix but `!` into themselves, and leave `!` outside
# (see parse_heap_alloc). An operand carrying a `!` anywhere on its suffix
# spine therefore has to keep its parentheses: written bare, `&(f.A!)` comes
# back as `(&f.A)!`, which is a different value.

enum Foo(A{x:Int}, B{t:Text})

struct Holder{opt:Foo?}

func main()
    f := Foo.A{123}
    # The `!` is the operand's, so the parentheses stay:
    >> &(f.A!)
    >> @(f.A!)
    # ...however deep on the spine it sits:
    >> &(f.A!.x)
    h := Holder{f}
    >> &(h.opt!.A!)
    # Suffixes that do bind inside need no parentheses:
    >> &f
    >> &f.A
    >> @f.A
