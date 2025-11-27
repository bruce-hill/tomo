# Tests for ensuring immutable value nature in various contexts
struct Inner(xs:[Int32])

struct Outer(inner:Inner)

func sneaky(outer:Outer)
    (&outer.inner.xs)[1] = 99

func sneaky2(outer:&Outer)
    (&outer.inner.xs)[1] = 99

func main()
    do
        xs := [10, 20, 30]
        copy := xs
        (&xs)[1] = 99
        assert xs == [99, 20, 30]
        assert copy == [10, 20, 30]

    do
        t := {"A":10, "B":20}
        copy := t
        (&t)["A"] = 99
        assert t == {"A":99, "B":20}
        assert copy == {"A":10, "B":20}

    do
        foo := Outer(Inner([10, 20, 30]))
        copy := foo
        (&foo.inner.xs)[1] = 99
        assert foo.inner.xs == [99, 20, 30]
        assert copy.inner.xs == [10, 20, 30]

    do
        foo := Outer(Inner([10, 20, 30]))
        copy := foo
        sneaky(foo)
        assert foo.inner.xs == [10, 20, 30]
        assert copy.inner.xs == [10, 20, 30]

    do
        foo := Outer(Inner([10, 20, 30]))
        copy := foo
        sneaky2(&foo)
        assert foo.inner.xs == [99, 20, 30]
        assert copy.inner.xs == [10, 20, 30]

