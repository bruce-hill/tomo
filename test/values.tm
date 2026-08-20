# Tests for ensuring immutable value nature in various contexts
struct Inner{xs:[Int32]}

struct Outer{inner:Inner}

enum HoldsList(HasList{xs:[Int32]})

func sneaky(outer:Outer)
    (&outer.inner.xs)[1] = 99

func sneaky2(outer:&Outer)
    (&outer.inner.xs)[1] = 99

test "list value semantics"
    >> xs := [10, 20, 30]
    >> copy := xs
    (&xs)[1] = 99
    >> xs
    assert xs == [99, 20, 30]
    >> copy
    assert copy == [10, 20, 30]

test "table value semantics"
    >> t := {"A":10, "B":20}
    >> copy := t
    (&t)["A"] = 99
    >> t
    assert t == {"A":99, "B":20}
    >> copy
    assert copy == {"A":10, "B":20}

test "nested struct field mutation"
    >> foo := Outer{Inner{[10, 20, 30]}}
    >> copy := foo
    (&foo.inner.xs)[1] = 99
    >> foo.inner.xs
    assert foo.inner.xs == [99, 20, 30]
    >> copy.inner.xs
    assert copy.inner.xs == [10, 20, 30]

test "struct passed by value is immutable"
    >> foo := Outer{Inner{[10, 20, 30]}}
    >> copy := foo
    sneaky(foo)
    >> foo.inner.xs
    assert foo.inner.xs == [10, 20, 30]
    >> copy.inner.xs
    assert copy.inner.xs == [10, 20, 30]

test "struct passed by reference is mutable"
    >> foo := Outer{Inner{[10, 20, 30]}}
    >> copy := foo
    sneaky2(&foo)
    >> foo.inner.xs
    assert foo.inner.xs == [99, 20, 30]
    >> copy.inner.xs
    assert copy.inner.xs == [10, 20, 30]

test "enum field value semantics"
    >> x := HoldsList.HasList{[10, 20, 30]}
    when x is HasList{list}
        (&list)[1] = 99

    >> x
    assert x == HoldsList.HasList{[10, 20, 30]}
