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

    # A receiver that a suffix would otherwise be read as part of keeps its
    # parentheses: `.f()` binds to the `else` branch of a bare `if`, and `@`
    # and `&` bind a method call into themselves the way they do a field.
    >> (if yes then f else f).A
    >> (match f case A{x} then f else f).A
    >> (@f).as_text()
    >> (&f).as_text()
    # ...while the `@` that really does own its method call needs none:
    >> @f.as_text()
