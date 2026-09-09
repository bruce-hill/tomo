func is_true(b:Bool -> Bool)
    return b

func add(x:Int, y:Int -> Int)
    return x + y

func cached_heap(x:Int->@Int; cached)
    return @x

# A signature wide enough to wrap puts its return type on a line of its own,
# with the `)` on the line after. An anonymous function's used to be the one
# that wouldn't take it, so the formatter had nowhere to put a lambda's.
func wrapped(
    first_argument:Int,
    second_argument:Int,
    -> Int
)
    return first_argument + second_argument

test "a signature that wraps"
    assert wrapped(1, 2) == 3
    lambda := func(
        first_argument:Int,
        second_argument:Int,
        -> Int
    )
        first_argument*second_argument
    assert lambda(3, 4) == 12
    no_args := func(-> Int
    ) 7
    assert no_args() == 7

test "basic function call"
    >> add(3, 5)
    assert add(3, 5) == 8

# Two flags are separated by a comma. Written `; cached; inline` -- which is
# what the formatter used to emit for this -- it doesn't parse.
func cached_and_inline(x:Int -> Int; cached, inline)
    return x + 1

test "two flags at once"
    assert cached_and_inline(1) == 2
    assert cached_and_inline(1) == 2

test "cached functions"
    >> cached_heap(1)
    >> cached_heap(2)
    assert cached_heap(1) == cached_heap(1)
    assert cached_heap(1) != cached_heap(2)

test "calling a function with too many arguments is rejected"
    _ := add(1, 2, 3)
fails_compile "This function's signature doesn't match this call site."

test "calling a function with too few arguments is rejected"
    _ := add(1)
fails_compile "This function's signature doesn't match this call site."

# A bare name followed by `==` inside an argument list is a comparison, not a
# named argument:
test "comparisons as arguments"
    x := 3
    y := 5
    assert not is_true(x == y)
    assert is_true(x == 3)
    assert is_true(x == y or x == 3)
