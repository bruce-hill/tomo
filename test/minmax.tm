
struct Foo{x:Int, y:Int}
	func len(f:Foo->Float64)
		return Float64.sqrt(Float64(f.x*f.x + f.y*f.y))!

test "min and max operators"
	>> (3 _min_ 5)
	assert (3 _min_ 5) == 3
	>> (5 _min_ 3)
	assert (5 _min_ 3) == 3
	>> (Foo{5, 1} _min_ Foo{5, 999})
	assert (Foo{5, 1} _min_ Foo{5, 999}) == Foo{x=5, y=1}
	>> (Foo{5, 999} _min_.x Foo{5, 1})
	assert (Foo{5, 999} _min_.x Foo{5, 1}) == Foo{x=5, y=999}
	>> (Foo{999, 1} _min_.y Foo{1, 10})
	assert (Foo{999, 1} _min_.y Foo{1, 10}) == Foo{x=999, y=1}
	>> (Foo{-999, -999} _max_.len() Foo{10, 10})
	assert (Foo{-999, -999} _max_.len() Foo{10, 10}) == Foo{x=-999, y=-999}
	>> foos := [Foo{5, 1}, Foo{5, 99}, Foo{-999, -999}]
	>> (_max_: foos)
	assert (_max_: foos)! == Foo{x=5, y=99}

# `_min_`/`_max_` are ordinary infix operators: whatever follows them stays part
# of the same expression.
test "min/max alongside other operators"
	assert 3 _min_ 5 == 3
	assert 1 _min_ 2 _min_ 3 == 1
	assert 5 _max_ 2 _max_ 9 == 9
	assert 2 + 3 _min_ 10 == 5
