# A top-level variable can be read by any function in the file, whether the
# function is defined above or below it, whether the variable is private (its
# name starts with `_`) or visible to importers, and whether its value is a
# static initializer or assigned by the file's initializer at startup.

func compute(-> Int)
    return 5

func read_above(-> [Int])
    return [lazy, _lazy_private, constant, _constant_private]

lazy := compute()
_lazy_private := compute() + 1
constant := 100
_constant_private := 200

func read_below(-> [Int])
    return [lazy, _lazy_private, constant, _constant_private]

struct Holder{x:Int}
    member := "abc".length + 12

    func read(-> Int)
        return Holder.member

test "reading top-level variables from a function defined above them"
    >> read_above()
    assert read_above() == [5, 6, 100, 200]

test "reading top-level variables from a function defined below them"
    assert read_below() == read_above()

test "reading a namespace variable"
    >> Holder.read()
    assert Holder.read() == 15
    assert Holder.member == 15
