struct Foo{x:Int}
    func update(f:&Foo)
        f.x += 1

struct Baz{foo:Foo}
    func update(b:&Baz)
        # Make sure & propagates here!
        b.foo.update()

test "nested pointer propagation"
    >> b := Baz{Foo{123}}
    >> b.foo.x
    b.update()
    >> b
    >> b.foo.x
    assert b.foo.x == 124

test "writing through a pointer counts as using it"
    # A variable that's only ever written *through* still affects whatever it
    # points at, so it doesn't count as an unused variable:
    x := 123
    p := &x
    p[] = 456
    >> x
    assert x == 456

    nums := @[1, 2, 3]
    entries := &nums
    entries[1] = 10
    >> nums
    assert nums[] == [10, 2, 3]

    foo := @Foo{1}
    field := &foo
    field.x = 99
    >> foo.x
    assert foo.x == 99
