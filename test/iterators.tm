
enum PairIteration(Done, Next(x:Text, y:Text))
func pairwise(strs:[Text])->func()->PairIteration:
    i := 1
    return func():
        if i + 1 > strs.length: return PairIteration.Done
        i += 1
        return PairIteration.Next(strs[i-1], strs[i])

enum RangeIteration(Done, Next(i:Int))
func range(first:Int, last:Int)->func()->RangeIteration:
    i := first
    return func():
        if i > last:
            return RangeIteration.Done
        i += 1
        return RangeIteration.Next(i-1)

func main():
    values := ["A", "B", "C", "D"]

    >> ((++) "($(foo)$(baz))" for foo, baz in pairwise(values))
    = "(AB)(BC)(CD)"
    >> ["$(foo)$(baz)" for foo, baz in pairwise(values)]
    = ["AB", "BC", "CD"]

    do:
        result := [:Text]
        for foo, baz in pairwise(values):
            result:insert("$(foo)$(baz)")
        >> result
        = ["AB", "BC", "CD"]

    >> [i for i in range(5, 10)]
    = [5, 6, 7, 8, 9, 10]

    >> (+) range(5, 10)
    = 45
