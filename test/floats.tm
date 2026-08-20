test "basic float arithmetic"
	>> n := Float64(1.5)
	>> n + n
	>> n * 2
	>> n - n
	assert n == 1.5
	assert n + n == 3.
	assert n * 2 == 3.
	assert n - n == 0.

test "float literals are inexact"
	>> Float64(0.1) + Float64(0.2)
	assert Float64(0.1) + Float64(0.2) != Float64(0.3)
	assert (Float64(0.1) + Float64(0.2)).near(0.3)

test "floored division"
	# `/` is ordinary float division; `//` is floor(x/y) -- plain floor, not
	# the Euclidean quotient the integer types and Num use, so for a negative
	# divisor it rounds down rather than keeping the remainder non-negative.
	assert Float64(7.5) / Float64(2) == 3.75
	assert Float64(7.5) // Float64(2) == 3.
	assert Float64(-7.5) // Float64(2) == -4.
	assert Float64(7.5) // Float64(-2) == -4.
	assert Float32(7.5) // Float32(2) == Float32(3)

test "constants"
	>> Float64.PI
	>> Float64.PI.with_precision(0.01)
	>> Float64.INF
	>> Float64.INF.isinf()
	assert Float64.PI == 3.141592653589793
	assert Float64.PI.with_precision(0.01) == 3.14
	assert Float64.INF == Float64.INF
	assert Float64.INF.isinf()

test "optional none floats"
	>> none_num : Float64? = none
	>> none_num
	assert none_num == none
	assert none_num == none_num
	assert not (none_num < none_num)
	assert not (none_num > none_num)
	assert not (none_num != none_num)
	assert (none_num <> none_num) == Int32(0)
	assert not (none_num == 0.0)
	assert none_num < 0.0
	assert not (none_num > 0.0)
	assert none_num != 0.0
	assert (none_num <> 0.0) == Int32(-1)

test "math functions"
	>> Float64.PI.cos()!
	>> Float64.PI.sin()!
	>> Float32.sqrt(16)
	>> Float32.sqrt(-1)
	>> Float64(0.25).mix(10, 20)
	>> Float64(2.0).mix(10, 20)
	>> Float64(5)
	>> Float64(0.5).percent()
	assert Float64.PI.cos()!.near(-1)
	assert Float64.PI.sin()!.near(0)
	assert not Float64.INF.near(-Float64.INF)
	assert Float32.sqrt(16) == Float32(4)
	assert Float32.sqrt(-1) == none
	assert Float64(0.25).mix(10, 20) == 12.5
	assert Float64(2.0).mix(10, 20) == 30.
	assert Float64(5) == 5.
	assert Float64(0.5).percent() == "50%"

test "force-unwrapping the square root of a negative panics"
	_ := Float64.sqrt(-1.0)!
fails "This was expected to be a value, but it's `none`"
