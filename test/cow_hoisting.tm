# Copy-on-write guard hoisting (see cow_hoist_env in compile/loops.c):
# loops whose bodies provably can't snapshot or resize a list compile its
# indexed writes without per-write CoW guards, after one up-front
# compact-if-shared. These tests pin down the observable semantics, which
# must be identical whether or not the optimization fires.

test "snapshot before a hoist-eligible loop is protected"
	xs := @[10, 20, 30]
	before := xs[]
	n := Int64(3)
	for i in Int64(1).to(n)
		xs[i] = xs[i]! * 100
	assert xs[] == [1000, 2000, 3000]
	assert before == [10, 20, 30]

test "snapshot taken mid-loop still gets copy-on-write"
	# The list-typed assignment makes this loop ineligible for hoisting,
	# so the ordinary per-write CoW guard must protect the snapshot.
	ys := @[1, 2, 3]
	mid : [Int] = []
	for i in Int64(1).to(Int64(3))
		if i == 2
			mid = ys[]
		ys[i] = ys[i]! + 10
	assert ys[] == [11, 12, 13]
	assert mid == [11, 2, 3]

test "aliased pointers share writes inside a hoisted loop"
	zs := @[1, 1, 1]
	alias := zs
	for i in Int64(1).to(Int64(3))
		zs[i] = zs[i]! + 1
		alias[i] = alias[i]! * 10
	assert zs[] == [20, 20, 20]
	assert alias[] == [20, 20, 20]

test "swap() composes with hoisting and still protects prior snapshots"
	vs := @[1, 2, 3, 4]
	before := vs[]
	for i in Int64(1).to(Int64(2))
		vs.swap(i, i + 2)
	assert vs[] == [3, 4, 1, 2]
	assert before == [1, 2, 3, 4]

test "hoisted writes accumulate across outer-loop iterations"
	ws := @[0, 0, 0]
	count := Int64(0)
	while count < 4
		for i in Int64(1).to(Int64(3))
			ws[i] = ws[i]! + i
		count += 1
	assert ws[] == [4, 8, 12]
