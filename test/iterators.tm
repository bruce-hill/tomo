
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

func main()
    values := ["A", "B", "C", "D"]

    >> (++: "($(foo.x)$(foo.y))" for foo in pairwise(values))!
    = "(AB)(BC)(CD)"
    >> ["$(foo.x)$(foo.y)" for foo in pairwise(values)]
    = ["AB", "BC", "CD"]

    do
        result : @[Text]
        for foo in pairwise(values)
            result.insert("$(foo.x)$(foo.y)")
        >> result[]
        = ["AB", "BC", "CD"]

    >> [i for i in range(5, 10)]
    = [5, 6, 7, 8, 9, 10]

    >> (+: range(5, 10))!
    = 45
