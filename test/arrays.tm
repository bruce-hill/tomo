func main():
	do:
		>> [:Num32]
		= [] : [Num32]

	do:
		>> arr := [10, 20, 30]
		= [10, 20, 30]

		>> arr[1]
		= 10
		>> arr[-1]
		= 30

		>> #arr
		= 3

		sum := 0
		for x in arr:
			sum += x
		>> sum
		= 60

		str := ""
		for i,x in arr:
			str ++= "({i},{x})"
		>> str
		= "(1,10)(2,20)(3,30)"

	do:
		>> arr := [10, 20] ++ [30, 40]
		= [10, 20, 30, 40]

		>> arr ++= [50, 60]
		>> arr
		= [10, 20, 30, 40, 50, 60]

		>> arr ++= 70
		>> arr
		= [10, 20, 30, 40, 50, 60, 70]

	do:
		>> arr := [10, 20]
		>> copy := arr
		>> arr ++= 30
		>> arr
		= [10, 20, 30]
		>> copy
		= [10, 20]

	do:
		>> [10*i for i in 5]
		= [10, 20, 30, 40, 50]

	>> [i*10 for i in 5]
	= [10, 20, 30, 40, 50]

	>> [i*10 for i in 5 if i mod 2 != 0]
	= [10, 30, 50]

	>> [x for x in y if x > 1 for y in [3, 4, 5] if y < 5]
	= [2, 3, 2, 3, 4]

	do:
		>> arr := @[10, 20]
		>> copy := arr[]
		>> arr:insert(30)
		>> arr
		= @[10, 20, 30]
		>> copy
		= [10, 20]

		>> arr[1] = 999
		= @[999, 20, 30]

	do:
		>> arr := [10, 20, 30]
		>> arr:reversed()
		= [30, 20, 10]

	do:
		>> nums := [10, -20, 30]
		// Sorted function doesn't mutate original:
		>> nums:sorted()
		= [-20, 10, 30]
		>> nums
		= [10, -20, 30]
		// Sort function does mutate in place:
		>> nums:sort()
		>> nums
		= [-20, 10, 30]
		// Custom sort functions:
		>> nums:sort(func(x:&%Int,y:&%Int): x:abs() <> y:abs())
		>> nums
		= [10, -20, 30]
		>> nums:sort(func(x:&%Int,y:&%Int): y[] <> x[])
		>> nums
		= [30, 10, -20]

	>> ["A", "B", "C"]:sample(10, [1.0, 0.5, 0.0])

	do:
		>> heap := [Int.random(max=50) for _ in 10]
		>> heap:heapify()
		>> heap
		sorted := [:Int]
		while #heap > 0:
			sorted:insert(heap:heap_pop())
		>> sorted == sorted:sorted()
		= yes
		for _ in 10:
			heap:heap_push(Int.random(max=50))
		>> heap
		sorted = [:Int]
		while #heap > 0:
			sorted:insert(heap:heap_pop())
		>> sorted == sorted:sorted()
		= yes

