func main()
	do
		nums : [Num32] = []
		assert nums == []

	do
		nums : [Num32]
		assert nums == []

	do
		list := [10, 20, 30]
		assert list == [10, 20, 30]

		assert list[1] == 10
		assert list[-1] == 30

		assert list.length == 3

		sum := 0
		for x in list
			sum += x
		assert sum == 60

		str := ""
		for i,x in list
			str ++= "($i,$x)"
		assert str == "(1,10)(2,20)(3,30)"

	do
		list := [10, 20] ++ [30, 40] 
		assert list == [10, 20, 30, 40]

		>> list ++= [50, 60]
		assert list == [10, 20, 30, 40, 50, 60]

	do
		>> list := [10, 20]
		>> copy := list
		>> list ++= [30]
		assert list == [10, 20, 30]
		assert copy == [10, 20]

	do
		assert [10*i for i in 5] == [10, 20, 30, 40, 50]

	assert [i*10 for i in 5] == [10, 20, 30, 40, 50]

	assert [i*10 for i in 5 if i mod 2 != 0] == [10, 30, 50]

	assert [x for x in y if x > 1 for y in [3, 4, 5] if y < 5] == [2, 3, 2, 3, 4]

	do
		>> list := @[10, 20]
		>> copy := list[]
		>> list.insert(30)
		assert list[] == [10, 20, 30]
		assert copy == [10, 20]

		>> list[1] = 999
		assert list[] == [999, 20, 30]

	do
		>> list := &[10, 20, 30]
		reversed := list.reversed()
		assert reversed == [30, 20, 10]
		# Ensure the copy-on-write behavior triggers:
		>> list[1] = 999
		assert reversed == [30, 20, 10]

	do
		>> nums := @[10, -20, 30]
		# Sorted function doesn't mutate original:
		assert nums.sorted() == [-20, 10, 30]
		assert nums[] == [10, -20, 30]
		# Sort function does mutate in place:
		>> nums.sort()
		assert nums[] == [-20, 10, 30]
		# Custom sort functions:
		>> nums.sort(func(x,y:&Int) x.abs() <> y.abs())
		assert nums[] == [10, -20, 30]
		>> nums.sort(func(x,y:&Int) y[] <> x[])
		assert nums[] == [30, 10, -20]

	>> ["A", "B", "C"].sample(10, [1.0, 0.5, 0.0])

	do
		>> heap := @[(i * 1337) mod 37 for i in 10]
		>> heap.heapify()
		>> heap
		heap_order : @[Int]
		repeat
			heap_order.insert(heap.heap_pop() or stop)
		assert heap_order[] == heap_order.sorted()
		heap_order[] = []
		for i in 10
			heap.heap_push((i*13337) mod 37)
		>> heap
		repeat
			heap_order.insert(heap.heap_pop() or stop)
		assert heap_order[] == heap_order.sorted()

	do
		assert [i*10 for i in 5].from(3) == [30, 40, 50]
		assert [i*10 for i in 5].to(3) == [10, 20, 30]
		assert [i*10 for i in 5].to(-2) == [10, 20, 30, 40]
		assert [i*10 for i in 5].from(-2) == [40, 50]

		assert [i*10 for i in 5].by(2) == [10, 30, 50]
		assert [i*10 for i in 5].by(-1) == [50, 40, 30, 20, 10]

		assert [10, 20, 30, 40].by(2) == [10, 30]
		assert [10, 20, 30, 40].by(-2) == [40, 20]

		assert [i*10 for i in 10].by(2).by(2) == [10, 50, 90]

		assert [i*10 for i in 10].by(2).by(-1) == [90, 70, 50, 30, 10]

		# Test iterating over list.from() and list.to()
		xs := ["A", "B", "C", "D"]
		for i,x in xs.to(-2)
			for y in xs.from(i+1)
				say("$(x)$(y)")

	do
		>> nums := @[-7, -4, -1, 2, 5]
		>> nums.sort()
		assert [nums.binary_search(i) for i in nums[]] == [1, 2, 3, 4, 5]
		>> nums.sort(func(a,b:&Int) a.abs() <> b.abs())
		assert [nums.binary_search(i, func(a,b:&Int) a.abs() <> b.abs()) for i in nums[]] == [1, 2, 3, 4, 5]

	assert ["a", "b", "c"].find("b") == 2
	assert ["a", "b", "c"].find("XXX") == none

	assert [10, 20].where(func(i:&Int) i.is_prime()) == none
	assert [4, 5, 6].where(func(i:&Int) i.is_prime()) == 2

	do
		>> nums := &[10, 20, 30, 40, 50]
		assert nums.pop() == 50
		assert nums[] == [10, 20, 30, 40]
		assert nums.pop(2) == 20
		assert nums[] == [10, 30, 40]
		>> nums.clear()
		assert nums[] == []
		assert nums.pop() == none
	
	assert [1,2,1,2,3].unique() == {1,2,3}
