test "empty list literal with type"
	>> nums : [Num32] = []
	assert nums == []

test "empty list declaration with type"
	nums : [Num32]
	>> nums
	assert nums == []

test "indexing, length, and iteration"
	>> list := [10, 20, 30]
	assert list == [10, 20, 30]
	>> list[1]
	assert list[1] == 10
	>> list[-1]
	assert list[-1] == 30
	>> list.length
	assert list.length == 3

	>> sum := 0
	for x in list
		>> sum += x
	>> sum
	assert sum == 60

	>> str := ""
	for i,x in list
		>> str ++= "($i,$x)"
	>> str
	assert str == "(1,10)(2,20)(3,30)"

test "concatenation"
	>> list := [10, 20] ++ [30, 40]
	assert list == [10, 20, 30, 40]
	>> list ++= [50, 60]
	assert list == [10, 20, 30, 40, 50, 60]

test "concatenation is copy-on-write"
	>> list := [10, 20]
	>> copy := list
	>> list ++= [30]
	assert list == [10, 20, 30]
	>> copy
	assert copy == [10, 20]

test "list comprehensions"
	>> [10*i for i in 5]
	assert [10*i for i in 5] == [10, 20, 30, 40, 50]
	>> [i*10 for i in 5]
	assert [i*10 for i in 5] == [10, 20, 30, 40, 50]
	>> [i*10 for i in 5 if i mod 2 != 0]
	assert [i*10 for i in 5 if i mod 2 != 0] == [10, 30, 50]
	>> [x for x in y if x > 1 for y in [3, 4, 5] if y < 5]
	assert [x for x in y if x > 1 for y in [3, 4, 5] if y < 5] == [2, 3, 2, 3, 4]

test "heap-allocated list mutation"
	>> list := @[10, 20]
	>> copy := list[]
	list.insert(30)
	>> list[]
	assert list[] == [10, 20, 30]
	>> copy
	assert copy == [10, 20]
	>> list[1] = 999
	>> list[]
	assert list[] == [999, 20, 30]

test "reversed is copy-on-write"
	>> list := &[10, 20, 30]
	>> reversed := list.reversed()
	assert reversed == [30, 20, 10]
	# Ensure the copy-on-write behavior triggers:
	>> list[1] = 999
	>> reversed
	assert reversed == [30, 20, 10]

test "sorting"
	>> nums := @[10, -20, 30]
	# Sorted function doesn't mutate original:
	>> nums.sorted()
	assert nums.sorted() == [-20, 10, 30]
	>> nums[]
	assert nums[] == [10, -20, 30]
	# Sort function does mutate in place:
	nums.sort()
	>> nums[]
	assert nums[] == [-20, 10, 30]
	# Custom sort functions:
	nums.sort(func(x,y:&Int) x.abs() <> y.abs())
	>> nums[]
	assert nums[] == [10, -20, 30]
	nums.sort(func(x,y:&Int) y[] <> x[])
	>> nums[]
	assert nums[] == [30, 10, -20]

test "weighted sample"
	>> ["A", "B", "C"].sample(10, [1.0, 0.5, 0.0])

test "heap operations"
	>> heap := @[(i * 1337) mod 37 for i in 10]
	heap.heapify()
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

test "slicing with from, to, and by"
	>> [i*10 for i in 5].from(3)
	assert [i*10 for i in 5].from(3) == [30, 40, 50]
	>> [i*10 for i in 5].to(3)
	assert [i*10 for i in 5].to(3) == [10, 20, 30]
	>> [i*10 for i in 5].to(-2)
	assert [i*10 for i in 5].to(-2) == [10, 20, 30, 40]
	>> [i*10 for i in 5].from(-2)
	assert [i*10 for i in 5].from(-2) == [40, 50]
	>> [i*10 for i in 5].by(2)
	assert [i*10 for i in 5].by(2) == [10, 30, 50]
	>> [i*10 for i in 5].by(-1)
	assert [i*10 for i in 5].by(-1) == [50, 40, 30, 20, 10]
	>> [10, 20, 30, 40].by(2)
	assert [10, 20, 30, 40].by(2) == [10, 30]
	>> [10, 20, 30, 40].by(-2)
	assert [10, 20, 30, 40].by(-2) == [40, 20]
	>> [i*10 for i in 10].by(2).by(2)
	assert [i*10 for i in 10].by(2).by(2) == [10, 50, 90]
	>> [i*10 for i in 10].by(2).by(-1)
	assert [i*10 for i in 10].by(2).by(-1) == [90, 70, 50, 30, 10]

	# Test iterating over list.from() and list.to()
	>> xs := ["A", "B", "C", "D"]
	for i,x in xs.to(-2)
		for y in xs.from(i+1)
			say("$(x)$(y)")

test "binary search"
	>> nums := @[-7, -4, -1, 2, 5]
	nums.sort()
	>> nums[]
	>> [nums.binary_search(func(x:&Int) x[] >= i) for i in nums[]]
	assert [nums.binary_search(func(x:&Int) x[] >= i) for i in nums[]] == [1, 2, 3, 4, 5]
	nums.sort(func(a,b:&Int) a.abs() <> b.abs())
	>> nums[]
	>> [nums.binary_search(func(x:&Int) x[].abs() >= i.abs()) for i in nums[]]
	assert [nums.binary_search(func(x:&Int) x[].abs() >= i.abs()) for i in nums[]] == [1, 2, 3, 4, 5]

test "find and where"
	>> ["a", "b", "c"].find("b")
	assert ["a", "b", "c"].find("b") == 2
	>> ["a", "b", "c"].find("XXX")
	assert ["a", "b", "c"].find("XXX") == none
	>> [10, 20].where(func(i:&Int) i.is_prime())
	assert [10, 20].where(func(i:&Int) i.is_prime()) == none
	>> [4, 5, 6].where(func(i:&Int) i.is_prime())
	assert [4, 5, 6].where(func(i:&Int) i.is_prime()) == 2

test "pop and clear"
	>> nums := &[10, 20, 30, 40, 50]
	assert nums.pop() == 50
	>> nums[]
	assert nums[] == [10, 20, 30, 40]
	assert nums.pop(2) == 20
	>> nums[]
	assert nums[] == [10, 30, 40]
	nums.clear()
	>> nums[]
	assert nums[] == []
	assert nums.pop() == none

test "unique"
	>> [1,2,1,2,3].unique()
	assert [1,2,1,2,3].unique() == {1,2,3}

test "out-of-bounds force-unwrap panics"
	_ := [10, 20][99]!
fails "Invalid list index: 99"

test "index 0 is out of bounds (lists are 1-indexed)"
	xs := [10, 20]
	_ := xs[0]!
fails "Invalid list index: 0"

test "an empty list needs a type"
	x := []
fails_compile "I can't tell what type of items this empty list holds"

test "list elements must match the declared type"
	xs : [Int] = ["hello"]
fails_compile "I expected a Int here, but this is a Text"
