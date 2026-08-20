struct Vec{x,y:Int}

test "empty list literal with type"
	>> nums : [Float32] = []
	assert nums == []

test "empty list declaration with type"
	nums : [Float32]
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
	for x at i in list
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

test "swap"
	>> list := &[10, 20, 30]
	list.swap(1, 3)
	assert list[] == [30, 20, 10]
	# Negative indices count from the back:
	list.swap(2, -1)
	assert list[] == [30, 10, 20]
	# Self-swap is a no-op:
	list.swap(2, 2)
	assert list[] == [30, 10, 20]

test "swap is copy-on-write"
	>> list := &[10, 20, 30]
	>> snapshot := list[]
	list.swap(1, 3)
	assert list[] == [30, 20, 10]
	assert snapshot == [10, 20, 30]

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
	nums.sort(func(x,y:Int) x.abs() <> y.abs())
	>> nums[]
	assert nums[] == [10, -20, 30]
	nums.sort(func(x,y:Int) y <> x)
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
		heap_order.insert(heap.heap_pop() or break)
	assert heap_order[] == heap_order.sorted()
	heap_order[] = []
	for i in 10
		heap.heap_push((i*13337) mod 37)
	>> heap
	repeat
		heap_order.insert(heap.heap_pop() or break)
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
	for x at i in xs.to(-2)
		for y in xs.from(i+1)
			say("$(x)$(y)")

test "binary search"
	>> nums := @[-7, -4, -1, 2, 5]
	nums.sort()
	>> nums[]
	>> [nums.binary_search(func(x:Int) x >= i) for i in nums[]]
	assert [nums.binary_search(func(x:Int) x >= i) for i in nums[]] == [1, 2, 3, 4, 5]
	nums.sort(func(a,b:Int) a.abs() <> b.abs())
	>> nums[]
	>> [nums.binary_search(func(x:Int) x.abs() >= i.abs()) for i in nums[]]
	assert [nums.binary_search(func(x:Int) x.abs() >= i.abs()) for i in nums[]] == [1, 2, 3, 4, 5]

test "find and where"
	>> ["a", "b", "c"].find("b")
	assert ["a", "b", "c"].find("b") == 2
	>> ["a", "b", "c"].find("XXX")
	assert ["a", "b", "c"].find("XXX") == none
	>> [10, 20].where(func(i:Int) i.is_prime())
	assert [10, 20].where(func(i:Int) i.is_prime()) == none
	>> [4, 5, 6].where(func(i:Int) i.is_prime())
	assert [4, 5, 6].where(func(i:Int) i.is_prime()) == 2

test "by-value predicates and comparisons"
	# By-value closures on struct elements:
	pts := @[Vec{3, 1}, Vec{1, 2}, Vec{2, 0}]
	pts.sort(func(a,b:Vec) a.x <> b.x)
	assert pts[] == [Vec{1, 2}, Vec{2, 0}, Vec{3, 1}]
	assert pts.where(func(p:Vec) p.y == 0) == 2
	# By-value closures on optional elements:
	opts : [Int?] = [3, none, 1]
	assert opts.where(func(x:Int?) x == none) == 2
	assert opts.where(func(x:Int?) x == 1) == 3
	# Mutation safety: the by-value parameter is a copy, so mutating it inside
	# the predicate cannot change the source list.
	xs := @[10, 20, 30]
	matched := xs.where(func(i:Int)
		i += 100
		return i == 130
	)
	assert matched == 3
	assert xs[] == [10, 20, 30]
	# A directly-passed lambda is compiled with by-pointer args (no adapter);
	# a closure value goes through the generic by-value adapter. Both must agree.
	is_five := func(i:Int) i == 5
	assert [1, 5, 9].where(is_five) == 2
	cmp := func(a,b:Int) b <> a
	descending := @[3, 1, 2]
	descending.sort(cmp)
	assert descending[] == [3, 2, 1]

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

test "in-place iteration by reference"
	xs := @[10, 20, 30]
	for &x in xs
		x[] += 1
	assert xs[] == [11, 21, 31]

test "in-place iteration with an index variable"
	xs := @[10, 20, 30]
	for &x at i in xs
		x[] = x[] + i
	assert xs[] == [11, 22, 33]

test "in-place iteration preserves snapshots taken before the loop"
	xs := @[1, 2, 3]
	snapshot := xs[]
	for &x in xs
		x[] = x[] * 10
	assert xs[] == [10, 20, 30]
	assert snapshot == [1, 2, 3]

test "in-place iteration supports continue and break"
	xs := @[1, 2, 3, 4, 5]
	for &x at i in xs
		if i == 2
			continue
		if i == 4
			break
		x[] += 100
	assert xs[] == [101, 2, 103, 4, 5]

test "in-place iteration runs the else block on an empty list"
	xs : @[Int] = @[]
	ran_else := no
	for &x in xs
		x[] = 99
	else
		ran_else = yes
	assert ran_else

# Shared state for the 'for &' aliasing failure tests below:
ref_loop_alias : @[Int] = @[0]

func resize_ref_loop_alias()
	ref_loop_alias.insert(99)

func copy_ref_loop_alias(-> [Int])
	return ref_loop_alias[]

test "resizing a list during a 'for &' loop fails"
	xs := @[1, 2, 3]
	ref_loop_alias = xs
	for &x in xs
		resize_ref_loop_alias()
		x[] += 1
fails "The list was resized while a 'for &' loop"

test "copying a list during a 'for &' loop fails"
	xs := @[1, 2, 3]
	ref_loop_alias = xs
	for &x in xs
		_ := copy_ref_loop_alias()
		x[] += 1
fails "A copy of the list was made while a 'for &' loop"

test "a copy made during the FINAL iteration of a 'for &' loop still fails"
	# Regression: the per-iteration guard runs at the top of each iteration, so
	# without the post-loop check a final-iteration copy escaped detection and
	# the snapshot was silently corrupted by the iteration's later writes.
	xs := @[1, 2]
	ref_loop_alias = xs
	for &x at i in xs
		if i == 2
			_ := copy_ref_loop_alias()
		x[] += 100
fails "A copy of the list was made while a 'for &' loop"

test "a copy made before a 'break' exit of a 'for &' loop still fails"
	xs := @[1, 2, 3, 4]
	ref_loop_alias = xs
	for &x at i in xs
		if i == 2
			_ := copy_ref_loop_alias()
			break
		x[] += 10
fails "A copy of the list was made while a 'for &' loop"

test "a resize during the FINAL iteration of a 'for &' loop still fails"
	xs := @[7]
	ref_loop_alias = xs
	for &x in xs
		resize_ref_loop_alias()
		x[] += 1
fails "The list was resized while a 'for &' loop"

test "the list is live inside a 'for &' loop"
	xs := @[1, 2, 3]
	for &x at i in xs
		assert xs.length == 3
		if i == 1
			xs[3] = 30 # indexed writes through the name still work and are seen live
		x[] += 100
	assert xs[] == [101, 102, 130]

test "iterating a list value by reference is a compile error"
	xs := [1, 2, 3]
	for &x in xs
		x[] += 1
fails_compile "can't be iterated by reference"

# A list literal whose elements are all compile-time constants is backed by a
# module-level `static const` array (see ConstList / is_constant). These tests
# guard that optimization: the value must survive escaping its stack frame, and
# any mutation must copy-on-write rather than write through the shared const data.
func constant_list_literal(-> [Int])
	return [10, 20, 30]

func nested_constant_literal(-> [[Int]])
	return [[1, 2], [3, 4]]

test "nested constant list literals are constant"
	assert nested_constant_literal() == [[1, 2], [3, 4]]

test "a constant list literal survives escaping its stack frame"
	# The returned value's buffer is static, so it stays valid after the
	# function that produced it has returned:
	assert constant_list_literal() == [10, 20, 30]
	got := constant_list_literal()
	assert got == [10, 20, 30]

test "mutating a constant-literal-backed list copies on write"
	list := @[10, 20, 30]
	snapshot := list[]
	list.insert(40)
	assert list[] == [10, 20, 30, 40]
	list[1] = 999
	assert list[] == [999, 20, 30, 40]
	# The snapshot and a second identical literal must read the original values,
	# proving the static const buffer was never written through:
	assert snapshot == [10, 20, 30]
	assert [10, 20, 30] == [10, 20, 30]
