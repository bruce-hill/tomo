
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
    assert {v: k for k, v in t} == {1: "a", 2: "b"}
    # nested comprehensions (later `for` is the outer loop)
    assert [i*10 + j for x at i in 2 for y at j in 2] == [Int64(11), Int64(21), Int64(12), Int64(22)]
    # reducers
    assert (+: i for x at i in xs)! == Int64(6)
    assert (+: x for x at i in xs if i != 2)! == 40
    assert (+: i for x at i in 2.to(5))! == Int64(10)
    assert (+: v for k, v in t)! == 3
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
