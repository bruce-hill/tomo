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
