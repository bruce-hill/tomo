func add(x:Int, y:Int -> Int):
	return x + y

func cached_heap(x:Int->@Int; cached):
	return @x

func main():
	>> add(3, 5)
	= 8

	>> cached_heap(1) == cached_heap(1)
	= yes
	>> cached_heap(1) == cached_heap(2)
	= no

