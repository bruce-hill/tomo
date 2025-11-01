struct Foo(x:Int)
    func update(f:&Foo)
        f.x += 1

struct Baz(foo:Foo)
    func update(b:&Baz)
        # Make sure & propagates here!
        b.foo.update()

func main()
    b := Baz(Foo(123))
    b.update()
    assert b.foo.x == 124
