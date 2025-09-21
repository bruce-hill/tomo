struct Foo(x,y:Int)

func main()
	assert (+: [10, 20, 30]) == 60

	>> empty_ints : [Int]
	assert (+: empty_ints) == none

	assert (+: [10, 20, 30]) or 0 == 60

	assert (+: empty_ints) or 0 == 0

	assert (_max_: [3, 5, 2, 1, 4]) == 5

	assert (_max_.abs(): [1, -10, 5]) == -10

	assert (_max_: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])! == Foo(x=1, y=0)
	assert (_max_.y: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])! == Foo(x=0, y=10)
	assert (_max_.y.abs(): [Foo(0, 0), Foo(1, 0), Foo(0, 10), Foo(0, -999)])! == Foo(x=0, y=-999)

	say("(or) and (and) have early out behavior:")
	assert (or: i == 3 for i in 9999999999999999999999999999)! == yes

	assert (and: i < 10 for i in 9999999999999999999999999999)! == no

	assert (<=: [1, 2, 2, 3, 4])! == yes

	assert (<=: empty_ints) == none

	assert (<=: [5, 4, 3, 2, 1])! == no

	assert (==: ["x", "y", "z"]) == no
	assert (==.length: ["x", "y", "z"]) == yes
	assert (+.length: ["x", "xy", "xyz"]) == 6

	assert (+.abs(): [1, 2, -3]) == 6
