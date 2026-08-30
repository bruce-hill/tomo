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

struct Money{amount, rate: Num}

struct Point{x, y: Float64}
    func scaled_by(p:Point, k:Float64 -> Point; inline)
        return Point{p.x*k, p.y*k}

    func len2(p:Point -> Float64; inline)
        return p.x*p.x + p.y*p.y

# The scan admits field reads, record literals, and calls that no list can
# cross (see cow_expr_ok). Each of these loops is eligible, so each must still
# leave a snapshot taken before it untouched.

test "field access and record literals stay eligible"
	ps := @[Point{1.0, 2.0}, Point{3.0, 4.0}]
	before := ps[]
	for i in Int64(1).to(Int64(2))
		p := ps[i]!
		ps[i] = Point{p.y, p.x}
	assert ps[] == [Point{2.0, 1.0}, Point{4.0, 3.0}]
	assert before == [Point{1.0, 2.0}, Point{3.0, 4.0}]

test "value method calls stay eligible"
	qs := @[Point{3.0, 4.0}, Point{6.0, 8.0}]
	before := qs[]
	for i in Int64(1).to(Int64(2))
		q := qs[i]!
		qs[i] = Point{q.len2(), 0.0}
	assert qs[] == [Point{25.0, 0.0}, Point{100.0, 0.0}]
	assert before == [Point{3.0, 4.0}, Point{6.0, 8.0}]

test "namespaced calls stay eligible"
	rs := @[Point{9.0, 0.0}, Point{16.0, 0.0}]
	before := rs[]
	for i in Int64(1).to(Int64(2))
		r := rs[i]!
		rs[i] = Point{Float64.sqrt(r.x)!, r.y}
	assert rs[] == [Point{3.0, 0.0}, Point{4.0, 0.0}]
	assert before == [Point{9.0, 0.0}, Point{16.0, 0.0}]

test "struct operators stay eligible"
	ss := @[Point{1.0, 2.0}, Point{3.0, 4.0}]
	before := ss[]
	for i in Int64(1).to(Int64(2))
		ss[i] = ss[i]!.scaled_by(2.0)
	assert ss[] == [Point{2.0, 4.0}, Point{6.0, 8.0}]
	assert before == [Point{1.0, 2.0}, Point{3.0, 4.0}]

test "a list-typed call argument is still ineligible"
	# `has()` takes the list itself, so a call like this must keep the
	# ordinary per-write guard rather than being hoisted.
	ts := @[1, 2, 3]
	mid : [Int] = []
	for i in Int64(1).to(Int64(3))
		if ts.has(2)
			mid = ts[]
		ts[i] = ts[i]! + 10
	assert ts[] == [11, 12, 13]
	assert mid == [11, 2, 3]

# `Num` and `Int` are tagged words that may point at their own heap object,
# which is never a list, so lists of them are eligible too. Heap-backed values
# (a big rational, an irrational) are the interesting case: the up-front
# compact has to copy them without the collector losing track.

test "Num lists are eligible and stay exact"
	ns := @[1/3, Num.PI, 1/2 + (5.).sqrt()!/2]
	before := ns[]
	for i in Int64(1).to(Int64(3))
		ns[i] = ns[i]! * 3
	assert ns[1]! == 1
	assert ns[2]! == 3 * Num.PI
	assert before == [1/3, Num.PI, 1/2 + (5.).sqrt()!/2]

test "structs of Nums are eligible"
	struct_vals := @[Money{1/7, 7}, Money{1/3, 3}]
	before := struct_vals[]
	for i in Int64(1).to(Int64(2))
		m := struct_vals[i]!
		struct_vals[i] = Money{m.amount * m.rate, m.rate}
	assert struct_vals[] == [Money{1, 7}, Money{1, 3}]
	assert before == [Money{1/7, 7}, Money{1/3, 3}]
