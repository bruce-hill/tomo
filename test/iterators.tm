
struct Pair(x:Text, y:Text)

func pairwise(strs:[Text] -> func(->Pair?))
    i := 1
    return func(-> Pair?)
        i += 1
        return Pair(strs[i-1] or return none, strs[i] or return none)

func range(first:Int, last:Int -> func(->Int?))
    i := first
    return func(->Int?)
        if i > last
            return none
        i += 1
        return (i-1)

test "pairwise iterator comprehensions"
    >> values := ["A", "B", "C", "D"]
    >> (++: "($(foo.x)$(foo.y))" for foo in pairwise(values))
    assert (++: "($(foo.x)$(foo.y))" for foo in pairwise(values))! == "(AB)(BC)(CD)"
    >> ["$(foo.x)$(foo.y)" for foo in pairwise(values)]
    assert ["$(foo.x)$(foo.y)" for foo in pairwise(values)] == ["AB", "BC", "CD"]

test "pairwise iterator in for loop"
    >> values := ["A", "B", "C", "D"]
    result : @[Text]
    for foo in pairwise(values)
        >> "$(foo.x)$(foo.y)"
        result.insert("$(foo.x)$(foo.y)")
    >> result[]
    assert result[] == ["AB", "BC", "CD"]

test "range iterator"
    >> [i for i in range(5, 10)]
    assert [i for i in range(5, 10)] == [5, 6, 7, 8, 9, 10]
    >> (+: range(5, 10))
    assert (+: range(5, 10))! == 45

test "loop indices are always Int64"
    xs := [10, 20, 30]
    for x at i in xs
        typed : Int64 = i # the index is a native Int64
        assert xs[typed]! == x
        _ : Int = i + 1 # ...and promotes to Int seamlessly

test "integer-count loop variables match the count's type"
    total := 0
    for x in 5
        typed : Int = x # `x` has the count's type
        total += typed
    assert total == 15

test "integer-count loops can have an Int64 index"
    n := 10
    for x at i in n
        _ : Int64 = i # the index is a native Int64
        _ : Int = x # ...and the value is an Int, like the count
        assert Int(i) == x
    big_hits := 0
    for x at i in Int(99999999999999)
        _ : Int64 = i
        _ : Int = x
        big_hits += 1
        if big_hits == 3
            stop
    assert big_hits == 3

test "ranges and iterator functions can have an Int64 index"
    # `for x at i in a.to(b)`: i counts 1, 2, 3, ... as Int64; x is the range value
    large := Int(99999999999999)
    for x at step in large.to(large+3)
        _ : Int64 = step
        assert large + Int(step) - 1 == x
    for x at step in Int64(10).to(Int64(13))
        assert Int64(9) + step == x
    # `for x at i in n.onward()`
    total := 0
    for x at i in Int(100).onward()
        total += x
        if i == Int64(3)
            stop
    assert total == 100 + 101 + 102
    # `for x at i in iterfn`
    got := ""
    for foo at i in pairwise(["A", "B", "C"])
        got ++= "$(i):$(foo.x)$(foo.y) "
    assert got == "1:AB 2:BC "

test "native Int64 counts are iterable"
    total : Int64 = 0
    for x in Int64(4)
        total += x
    assert total == Int64(10)
    for x at i in Int64(4)
        assert i == x
    ran_else := no
    for x in Int64(0)
        pass
    else
        ran_else = yes
    assert ran_else

test "text iteration indices are Int64 and 1-based"
    out := ""
    for c at i in "abc"
        out ++= "$(i)$(c)"
    assert out == "1a2b3c"

test "comprehensions and reducers support index variables"
    xs := [10, 20, 30]
    t := {"a": 1, "b": 2}
    # list comprehensions
    assert [i*x for x at i in xs] == [10, 40, 90]
    assert [x for x at i in xs if i mod 2 == 1] == [10, 30]
    assert [b for b at a in 3] == [1, 2, 3]
    assert [a for b at a in 5.to(7)] == [Int64(1), Int64(2), Int64(3)]
    # set and table comprehensions
    assert {i*x for x at i in xs} == {10, 40, 90}
    assert {i: x for x at i in xs} == {Int64(1): 10, Int64(2): 20, Int64(3): 30}
    assert {v: k for k, v in t.entries()} == {1: "a", 2: "b"}
    # nested comprehensions (later `for` is the outer loop)
    assert [i*10 + j for x at i in 2 for y at j in 2] == [Int64(11), Int64(21), Int64(12), Int64(22)]
    # reducers
    assert (+: i for x at i in xs)! == Int64(6)
    assert (+: x for x at i in xs if i != 2)! == 40
    assert (+: i for x at i in 2.to(5))! == Int64(10)
    assert (+: v for k, v in t.entries())! == 3
    assert (++: "$(i)$(c)" for c at i in "abc")! == "1a2b3c"
    assert (_max_: i*x for x at i in [30, 20, 10])! == 40

test "the old leading-index loop form is an error"
    for i, x in [10, 20, 30]
        pass
fails_compile "use `at`"

func int_pairs(xs:[Int] -> func(a:&Int, b:&Int -> Bool))
    i := 1
    j := 1
    return func(a:&Int, b:&Int -> Bool)
        j += 1
        if j > xs.length
            i += 1
            j = i + 1
        if j > xs.length
            return no
        a[] = xs[i]!
        b[] = xs[j]!
        return yes

func numbered(names:[Text] -> func(n:&Int64, name:&Text -> Bool))
    i := Int64(0)
    return func(n:&Int64, name:&Text -> Bool)
        i += 1
        if i > Int64(names.length)
            return no
        n[] = i
        name[] = names[i]!
        return yes

test "multi-value iterators yield through & out-parameters"
    xs := [1, 2, 3, 4]
    got := ""
    for a, b in int_pairs(xs)
        got ++= "$(a)$(b) "
    assert got == "12 13 14 23 24 34 "
    # with an `at` counter
    got2 := ""
    for a, b at i in int_pairs(xs)
        got2 ++= "$(i):$(a)$(b) "
    assert got2 == "1:12 2:13 3:14 4:23 5:24 6:34 "
    # heterogeneous yield types
    got3 := ""
    for n, name in numbered(["a", "b"])
        got3 ++= "$(n)$(name) "
    assert got3 == "1a 2b "
    # `_` discards bind nothing
    total := 0
    for a, _ in int_pairs(xs)
        total += a
    assert total == 1 + 1 + 1 + 2 + 2 + 3
    # comprehensions and reducers
    assert [a*10 + b for a, b in int_pairs(xs)] == [12, 13, 14, 23, 24, 34]
    assert (+: a*b for a, b in int_pairs(xs) if a != 1)! == 6 + 8 + 12
    # skip and stop
    got4 := ""
    for a, b at i in int_pairs(xs)
        if i == 2
            skip
        if i == 4
            stop
        got4 ++= "$(a)$(b) "
    assert got4 == "12 14 "
    # else clause runs when the iterator yields nothing
    ran_else := no
    for a, b in int_pairs([7])
        pass
    else
        ran_else = yes
    assert ran_else

test "multi-value iterator variable counts must match the yield count"
    xs := [1, 2, 3]
    for a in int_pairs(xs)
        pass
fails_compile "yields 2 values per iteration, but this loop has 1 variable"

test "a leading index variable on a multi-value iterator suggests `at`"
    xs := [1, 2, 3]
    for i, a, b in int_pairs(xs)
        pass
fails_compile "bind it with `at`"

test "lockstep iteration over multiple iterables"
    xs := [10, 20, 30]
    ys := ["a", "b", "c"]
    got := ""
    for x, y in xs, ys
        got ++= "$(x)$(y) "
    assert got == "10a 20b 30c "
    # the loop ends when the shortest iterable runs out (either side):
    assert ["$(x)$(y)" for x, y in xs, [1, 2]] == ["101", "202"]
    assert ["$(x)$(y)" for x, y in [1, 2], xs] == ["110", "220"]
    # `at` counts iterations alongside the lockstep values:
    assert ["$(i):$(x)$(y)" for x, y at i in xs, ys] == ["1:10a", "2:20b", "3:30c"]
    # `_` discards a value:
    assert [y for _, y in xs, ys] == ["a", "b", "c"]
    # any iterable kind can participate: counts, ranges, text, tables...
    assert ["$(n)$(x)" for n, x in 5, xs] == ["110", "220", "330"]
    assert ["$(r)$(x)" for r, x in 100.to(200), xs] == ["10010", "10120", "10230"]
    assert ["$(c)$(x)" for c, x in "abc", xs] == ["a10", "b20", "c30"]
    t := {"one": 1, "two": 2}
    assert ["$(k)$(v)$(x)" for k, v, x in t.entries(), xs] == ["one110", "two220"]
    s := {5, 6, 7}
    assert ["$(e)$(x)" for e, x in s, xs] == ["510", "620", "730"]
    # ...including multi-value iterator functions:
    assert ["$(a)$(b)$(y)" for a, b, y in int_pairs([1, 2, 3]), ys] == ["12a", "13b", "23c"]

test "lockstep iterables are evaluated left to right, exactly once"
    order : @[Text]
    first := func(-> [Int])
        order.insert("first")
        return [1, 2]
    second := func(-> [Int])
        order.insert("second")
        return [10, 20]
    for x, y in first(), second()
        pass
    assert order[] == ["first", "second"]

test "lockstep loops support skip, stop, and else"
    xs := [10, 20, 30]
    ys := ["a", "b", "c"]
    got := ""
    for x, y at i in xs, ys
        if i == 1
            skip
        if i == 3
            stop
        got ++= "$(x)$(y)"
    assert got == "20b"
    ran_else := no
    empty : [Int] = []
    for x, y in xs, empty
        pass
    else
        ran_else = yes
    assert ran_else
    # ...but not when every iterable has values:
    ran_else2 := no
    for x, y in xs, ys
        pass
    else
        ran_else2 = yes
    assert not ran_else2

test "lockstep reducers"
    # dot product, no intermediate list:
    assert (+: x*y for x, y in [1, 2, 3], [4, 5, 6])! == 32

test "reducers inside lambdas capture their iterables"
    xs := [1, 2, 3]
    ys := [10, 20, 30]
    f := func(-> Int)
        return (+: x*2 for x in xs) or 0
    assert f() == 12
    g := func(-> Int)
        return (+: a*b for a, b in xs, ys) or 0
    assert g() == 140

test "lockstep variable counts must match the iterables' total yields"
    t := {"one": 1}
    for k, x in t.entries(), [1, 2]
        pass
fails_compile "`t.entries()` yields 2 values, `[1, 2]` yields 1 value"

test "table.entries() iterates key/value pairs"
    t := {"one": 1, "two": 2, "three": 3}
    got := ""
    for k, v in t.entries()
        got ++= "$k=$v "
    assert got == "one=1 two=2 three=3 "
    # with an `at` counter, in comprehensions, and in reducers:
    assert ["$(i):$(k)$(v)" for k, v at i in t.entries()] == ["1:one1", "2:two2", "3:three3"]
    assert (+: v for _, v in t.entries())! == 6
    assert {v: k for k, v in t.entries()} == {1: "one", 2: "two", 3: "three"}
    # snapshot semantics: mutations after making the iterator aren't seen
    t2 := @{"a": 1}
    iter := t2.entries()
    t2.set("b", 2)
    assert ["$k$v" for k, v in iter] == ["a1"]

test "list.pairs() iterates each unordered pair once (i < j)"
    xs := [10, 20, 30, 40]
    assert ["$a-$b" for a, b in xs.pairs()] == ["10-20", "10-30", "10-40", "20-30", "20-40", "30-40"]
    assert (+: 1 for _, _ in [1, 2, 3].pairs()) == 3
    assert [b for _, b in [5, 6].pairs()] == [6]
    # too few elements for any pair:
    assert ["$a$b" for a, b in [7].pairs()] == []
    ran_else := no
    empty : [Int] = []
    for a, b in empty.pairs()
        pass
    else
        ran_else = yes
    assert ran_else
    # snapshot semantics: mutations after making the iterator aren't seen
    ys := @[1, 2]
    p := ys.pairs()
    ys[1] = 99
    assert ["$a$b" for a, b in p] == ["12"]

test "pairs()/entries() keep snapshot semantics when inlined in for-position"
    # These compile to inline index loops (no closure); mutating the container
    # in the body must still not affect the iteration (the snapshot is stable).
    xs := @[1, 2, 3]
    got := ""
    for a, b in xs.pairs()
        got ++= "$a$b "
        xs[1] = 99
    assert got == "12 13 23 "
    assert xs[1]! == 99
    t := @{"a": 1, "b": 2}
    got2 := ""
    for k, v in t.entries()
        got2 ++= "$k$v "
        t.set("c", 3)
    assert got2 == "a1 b2 "

test "tables can't be iterated directly"
    t := {"one": 1}
    for k, v in t
        pass
fails_compile "Use `.entries()`"

test "sets iterate their elements directly"
    s := {10, 20, 30}
    total := 0
    for x at i in s
        total += x + Int(i)
    assert total == 66
    assert (+: x for x in s)! == 60

test "by-reference variables aren't allowed in lockstep loops"
    xs := @[1, 2, 3]
    for &x, y in xs, [4, 5, 6]
        x[] += y
fails_compile "aren't supported when iterating over multiple values in lockstep"
