func add(x:Int, y:Int)->Int
	return x + y

>> add(3, 5)
= 8

func cached_heap(x:Int; cached)->@Int
	return @x

>> cached_heap(1) == cached_heap(1)
= yes
>> cached_heap(1) == cached_heap(2)
= no

