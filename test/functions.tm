func add(x:Int, y:Int -> Int)
	return x + y

func cached_heap(x:Int->@Int; cached)
	return @x

func main()
	assert add(3, 5) == 8

	assert cached_heap(1) == cached_heap(1)
	assert cached_heap(1) != cached_heap(2)

