
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
