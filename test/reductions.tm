struct Foo(x,y:Int)

test "sum reduction"
	>> (+: [10, 20, 30])
	assert (+: [10, 20, 30]) == 60
	>> empty_ints : [Int]
	>> (+: empty_ints)
	assert (+: empty_ints) == none
	>> (+: [10, 20, 30]) or 0
	assert (+: [10, 20, 30]) or 0 == 60
	>> (+: empty_ints) or 0
	assert (+: empty_ints) or 0 == 0

test "max reduction with field and method keys"
	>> (_max_: [3, 5, 2, 1, 4])
	assert (_max_: [3, 5, 2, 1, 4]) == 5
	>> (_max_.abs(): [1, -10, 5])
	assert (_max_.abs(): [1, -10, 5]) == -10
	>> (_max_: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])
	assert (_max_: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])! == Foo(x=1, y=0)
	>> (_max_.y: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])
	assert (_max_.y: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])! == Foo(x=0, y=10)
	>> (_max_.y.abs(): [Foo(0, 0), Foo(1, 0), Foo(0, 10), Foo(0, -999)])
	assert (_max_.y.abs(): [Foo(0, 0), Foo(1, 0), Foo(0, 10), Foo(0, -999)])! == Foo(x=0, y=-999)

test "or and and have early-out behavior"
	say("(or) and (and) have early out behavior:")
	>> (or: i == 3 for i in 9999999999999999999999999999)
	assert (or: i == 3 for i in 9999999999999999999999999999)! == yes
	>> (and: i < 10 for i in 9999999999999999999999999999)
	assert (and: i < 10 for i in 9999999999999999999999999999)! == no

test "comparison reductions"
	>> empty_ints : [Int]
	>> (<=: [1, 2, 2, 3, 4])
	assert (<=: [1, 2, 2, 3, 4])! == yes
	>> (<=: empty_ints)
	assert (<=: empty_ints) == none
	>> (<=: [5, 4, 3, 2, 1])
	assert (<=: [5, 4, 3, 2, 1])! == no

test "equality and field reductions"
	>> (==: ["x", "y", "z"])
	assert (==: ["x", "y", "z"]) == no
	>> (==.length: ["x", "y", "z"])
	assert (==.length: ["x", "y", "z"]) == yes
	>> (+.length: ["x", "xy", "xyz"])
	assert (+.length: ["x", "xy", "xyz"]) == 6
	>> (+.abs(): [1, 2, -3])
	assert (+.abs(): [1, 2, -3]) == 6
