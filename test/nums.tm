func main()
	n := 1.5
	assert n == 1.5

	assert n + n == 3.

	assert n * 2 == 3.

	assert n - n == 0.

	assert Num.PI == 3.141592653589793

	assert Num.PI.with_precision(0.01) == 3.14

	assert Num.INF == Num.INF
	assert Num.INF.isinf()

	none_num : Num? = none
	assert none_num == none
	assert none_num == none_num
	assert (none_num < none_num) == no
	assert (none_num > none_num) == no
	assert (none_num != none_num) == no
	assert (none_num <> none_num) == Int32(0)
	assert (none_num == 0.0) == no
	assert none_num < 0.0
	assert (none_num > 0.0) == no
	assert none_num != 0.0
	assert (none_num <> 0.0) == Int32(-1)

	# >> nan + 1
	# = none

	>> 0./0.

	# >> 0./0.
	# = none

	assert Num.PI.cos()!.near(-1)
	assert Num.PI.sin()!.near(0)

	assert Num.INF.near(-Num.INF) == no

	assert Num32.sqrt(16) == Num32(4)
	assert Num32.sqrt(-1) == none

	assert (0.25).mix(10, 20) == 12.5
	assert (2.0).mix(10, 20) == 30.

	assert Num(5) == 5.

	assert (0.5).percent() == "50%"
