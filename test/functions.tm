func is_true(b:Bool -> Bool)
	return b

func add(x:Int, y:Int -> Int)
	return x + y

func cached_heap(x:Int->@Int; cached)
	return @x

test "basic function call"
	>> add(3, 5)
	assert add(3, 5) == 8

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
