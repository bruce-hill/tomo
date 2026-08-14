struct Foo(x:Int)
    func update(f:&Foo)
        f.x += 1

struct Baz(foo:Foo)
    func update(b:&Baz)
        # Make sure & propagates here!
        b.foo.update()

test "nested pointer propagation"
    >> b := Baz(Foo(123))
    >> b.foo.x
    b.update()
    >> b
    >> b.foo.x
    assert b.foo.x == 124
