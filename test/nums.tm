test "basic float arithmetic"
	>> n := 1.5
	>> n + n
	>> n * 2
	>> n - n
	assert n == 1.5
	assert n + n == 3.
	assert n * 2 == 3.
	assert n - n == 0.

test "constants"
	>> Num.PI
	>> Num.PI.with_precision(0.01)
	>> Num.INF
	>> Num.INF.isinf()
	assert Num.PI == 3.141592653589793
	assert Num.PI.with_precision(0.01) == 3.14
	assert Num.INF == Num.INF
	assert Num.INF.isinf()

test "optional none nums"
	>> none_num : Num? = none
	>> none_num
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

test "math functions"
	>> Num.PI.cos()!
	>> Num.PI.sin()!
	>> Num32.sqrt(16)
	>> Num32.sqrt(-1)
	>> (0.25).mix(10, 20)
	>> (2.0).mix(10, 20)
	>> Num(5)
	>> (0.5).percent()
	assert Num.PI.cos()!.near(-1)
	assert Num.PI.sin()!.near(0)
	assert Num.INF.near(-Num.INF) == no
	assert Num32.sqrt(16) == Num32(4)
	assert Num32.sqrt(-1) == none
	assert (0.25).mix(10, 20) == 12.5
	assert (2.0).mix(10, 20) == 30.
	assert Num(5) == 5.
	assert (0.5).percent() == "50%"

test "force-unwrapping the square root of a negative panics"
	_ := Num.sqrt(-1.0)!
fails "This was expected to be a value, but it's `none`"
