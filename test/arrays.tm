func main()
	do
		>> nums : [Num32] = []
		= []

	do
		>> arr := [10, 20, 30]
		= [10, 20, 30]

		>> arr[1]
		= 10
		>> arr[-1]
		= 30

		>> arr.length
		= 3

		sum := 0
		for x in arr
			sum += x
		>> sum
		= 60

		str := ""
		for i,x in arr
			str ++= "($i,$x)"
		>> str
		= "(1,10)(2,20)(3,30)"

	do
		>> arr := [10, 20] ++ [30, 40]
		= [10, 20, 30, 40]

		>> arr ++= [50, 60]
		>> arr
		= [10, 20, 30, 40, 50, 60]

	do
		>> arr := [10, 20]
		>> copy := arr
		>> arr ++= [30]
		>> arr
		= [10, 20, 30]
		>> copy
		= [10, 20]

	do
		>> [10*i for i in 5]
		= [10, 20, 30, 40, 50]

	>> [i*10 for i in 5]
	= [10, 20, 30, 40, 50]

	>> [i*10 for i in 5 if i mod 2 != 0]
	= [10, 30, 50]

	>> [x for x in y if x > 1 for y in [3, 4, 5] if y < 5]
	= [2, 3, 2, 3, 4]

	do
		>> arr := @[10, 20]
		>> copy := arr[]
		>> arr.insert(30)
		>> arr
		= @[10, 20, 30]
		>> copy
		= [10, 20]

		>> arr[1] = 999
		>> arr
		= @[999, 20, 30]

	do
		>> arr := &[10, 20, 30]
		>> reversed := arr.reversed()
		= [30, 20, 10]
		# Ensure the copy-on-write behavior triggers:
		>> arr[1] = 999
		>> reversed
		= [30, 20, 10]

	do
		>> nums := @[10, -20, 30]
		# Sorted function doesn't mutate original:
		>> nums.sorted()
		= [-20, 10, 30]
		>> nums
		= @[10, -20, 30]
		# Sort function does mutate in place:
		>> nums.sort()
		>> nums
		= @[-20, 10, 30]
		# Custom sort functions:
		>> nums.sort(func(x,y:&Int) x.abs() <> y.abs())
		>> nums
		= @[10, -20, 30]
		>> nums.sort(func(x,y:&Int) y[] <> x[])
		>> nums
		= @[30, 10, -20]

	>> ["A", "B", "C"].sample(10, [1.0, 0.5, 0.0])

	do
		>> heap := @[(i * 1337) mod 37 for i in 10]
		>> heap.heapify()
		>> heap
		heap_order : @[Int] = @[]
		repeat
			heap_order.insert(heap.heap_pop() or stop)
		>> heap_order[] == heap_order.sorted()
		= yes
		heap_order[] = []
		for i in 10
			heap.heap_push((i*13337) mod 37)
		>> heap
		repeat
			heap_order.insert(heap.heap_pop() or stop)
		>> heap_order[] == heap_order.sorted()
		= yes

	do
		>> [i*10 for i in 5].from(3)
		= [30, 40, 50]
		>> [i*10 for i in 5].to(3)
		= [10, 20, 30]
		>> [i*10 for i in 5].to(-2)
		= [10, 20, 30, 40]
		>> [i*10 for i in 5].from(-2)
		= [40, 50]

		>> [i*10 for i in 5].by(2)
		= [10, 30, 50]
		>> [i*10 for i in 5].by(-1)
		= [50, 40, 30, 20, 10]

		>> [10, 20, 30, 40].by(2)
		= [10, 30]
		>> [10, 20, 30, 40].by(-2)
		= [40, 20]

		>> [i*10 for i in 10].by(2).by(2)
		= [10, 50, 90]

		>> [i*10 for i in 10].by(2).by(-1)
		= [90, 70, 50, 30, 10]

		# Test iterating over array.from() and array.to()
		xs := ["A", "B", "C", "D"]
		for i,x in xs.to(-2)
			for y in xs.from(i+1)
				say("$(x)$(y)")

	do
		>> nums := @[-7, -4, -1, 2, 5]
		>> nums.sort()
		>> [nums.binary_search(i) for i in nums[]]
		= [1, 2, 3, 4, 5]
		>> nums.sort(func(a,b:&Int) a.abs() <> b.abs())
		>> [nums.binary_search(i, func(a,b:&Int) a.abs() <> b.abs()) for i in nums[]]
		= [1, 2, 3, 4, 5]

	>> ["a", "b", "c"].find("b")
	= 2?
	>> ["a", "b", "c"].find("XXX")
	= none

	>> [10, 20].first(func(i:&Int) i.is_prime())
	= none
	>> [4, 5, 6].first(func(i:&Int) i.is_prime())
	= 2?

	do
		>> nums := &[10, 20, 30, 40, 50]
		>> nums.pop()
		= 50?
		>> nums
		= &[10, 20, 30, 40]
		>> nums.pop(2)
		= 20?
		>> nums
		= &[10, 30, 40]
		>> nums.clear()
		>> nums
		= &[]
		>> nums.pop()
		= none
