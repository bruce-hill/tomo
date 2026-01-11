func main()
	n := Float64(1.5)
	assert n == Float64(1.5)

	assert n + n == Float64(3.)

	assert n * 2 == Float64(3.)

	assert n - n == Float64(0.)

	assert Float64.PI == Float64(3.141592653589793)

	assert Float64.PI.with_precision(Float64(0.01)) == Float64(3.14)

	assert Float64.INF == Float64.INF
	assert Float64.INF.isinf()

	none_num : Float64? = none
	assert none_num == none
	assert none_num == none_num
	assert (none_num < none_num) == no
	assert (none_num > none_num) == no
	assert (none_num != none_num) == no
	assert (none_num <> none_num) == Int32(0)
	assert (none_num == Float64(0.0)) == no
	assert none_num < Float64(0.0)
	assert (none_num > Float64(0.0)) == no
	assert none_num != Float64(0.0)
	assert (none_num <> Float64(0.0)) == Int32(-1)

	# >> nan + 1
	# = none

	>> Float64(0.)/Float64(0.)

	# >> Float64(0.)/Float64(0.)
	# = none

	assert Float64.PI.cos()!.near(Float64(-1))
	assert Float64.PI.sin()!.near(Float64(0))

	assert Float64.INF.near(-Float64.INF) == no

	assert Float32.sqrt(16) == Float32(4)
	assert Float32.sqrt(-1) == none

	assert (Float64(0.25)).mix(10, 20) == Float64(12.5)
	assert (Float64(2.0)).mix(10, 20) == Float64(30.)

	assert Float64(5) == Float64(5.)

	assert (Float64(0.5)).percent() == "50%"
