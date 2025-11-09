func main()
	n := 1.5
	assert n == 1.5

	assert n + n == 3.

	assert n * 2 == 3.

	assert n - n == 0.

	assert Float64.PI == 3.141592653589793

	assert Float64.PI.with_precision(0.01) == 3.14

	assert Float64.INF == Float64.INF
	assert Float64.INF.isinf()

	none_num : Float64? = none
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

	assert Float64.PI.cos()!.near(-1)
	assert Float64.PI.sin()!.near(0)

	assert Float64.INF.near(-Float64.INF) == no

	assert Float32.sqrt(16) == Float32(4)
	assert Float32.sqrt(-1) == none

	assert (0.25).mix(10, 20) == 12.5
	assert (2.0).mix(10, 20) == 30.

	assert Float64(5) == 5.

	assert (0.5).percent() == "50%"
