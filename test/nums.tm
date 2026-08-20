test "numeric literals are exact"
	>> 0.1 + 0.2
	assert 0.1 + 0.2 == 0.3
	assert 0.1 + 0.2 - 0.3 == 0
	assert 1.1 * 3 == 3.3
	assert 3.15 == 63/20.

test "fractions stay exact"
	>> 1./3.
	assert 1./3. == 1./3.
	assert (1./3.) * 3 == 1
	assert (1./3.) + (1./6.) == 0.5
	assert (1./7.) * 7 == 1

test "big values don't overflow or lose precision"
	>> (2.).power(100)
	assert (2.).power(100) == 1267650600228229401496703205376
	assert (2.).power(100) / (2.).power(100) == 1
	assert (1./(2.).power(100)) * (2.).power(100) == 1

test "irrational values are exact"
	>> (2.).sqrt()!
	assert (2.).sqrt()! * (2.).sqrt()! == 2
	assert (8.).sqrt()! == (2.).sqrt()! * 2
	assert (16.).sqrt()! == 4
	assert (2.).sqrt()!.is_rational() == no
	assert (4.).sqrt()!.is_rational() == yes

test "pi"
	>> Num.PI
	assert Num.PI.sin() == 0
	assert Num.PI.cos() == -1
	assert Num.TAU == 2 * Num.PI
	assert (Num.PI / 2).sin() == 1
	assert Num.PI.is_rational() == no

test "degrees and percents"
	assert 180deg == Num.PI
	assert 90deg == Num.PI / 2
	assert 50% == 0.5
	assert 25% == 1./4.

test "display shows exact values"
	# A terminating decimal prints as one; anything else prints in the exact
	# form it has, never as a rounded decimal.
	assert "$(0.1 + 0.2)" == "0.3"
	assert "$(1./3.)" == "1/3"
	assert "$((2.).sqrt()!)" == "sqrt(2)"
	assert "$(Num.PI)" == "pi"
	assert "$(2. + 2.)" == "4"

test "approximating to a requested number of digits"
	>> (1./3.).digits(10)
	assert (1./3.).digits(10) == "0.3333333333"
	assert (1./3.).digits(0) == "0"
	assert Num.PI.digits(10) == "3.1415926536"
	assert (2.).sqrt()!.digits(10) == "1.4142135624"
	# An exact value stops early rather than padding zeros:
	assert (0.25).digits(10) == "0.25"

test "is_exact reports whether digits capture the value"
	assert (0.25).is_exact(10)
	assert not (1./3.).is_exact(10)
	assert not Num.PI.is_exact(1000)
	assert (0.1 + 0.2).is_exact(1)

test "symbolic and TeX forms"
	assert (1./3.).symbolic() == "1/3"
	assert (2.).sqrt()!.symbolic() == "sqrt(2)"
	assert Num.PI.symbolic() == "pi"
	assert (1./3.).tex() == "\\frac{1}{3}"
	assert (2.).sqrt()!.tex() == "\\sqrt{2}"

test "conversions"
	>> (0.5).to_float64()
	assert (0.5).to_float64() == Float64(0.5)
	assert (4.).to_int() == 4
	assert (4.5).to_int() == none
	assert (2.).sqrt()!.to_int() == none
	assert (4.5).floor().to_int() == 4
	assert (4.5).ceil().to_int() == 5

test "rounding"
	assert (4.5).floor() == 4
	assert (4.5).ceil() == 5
	assert (-4.5).trunc() == -4
	assert (2.5).round() == 2
	assert (3.5).round() == 4
	assert Num.PI.floor() == 3

test "comparisons"
	assert 0.1 + 0.2 <= 0.3
	assert 0.1 + 0.2 >= 0.3
	assert 1./3. < 0.34
	assert 1./3. > 0.33
	assert (2.).sqrt()! < 1.5
	assert (2.).sqrt()! > 1.4
	assert ((1./3.) <> (1./3.)) == Int32(0)

test "methods with a restricted domain return none"
	assert (-1.).sqrt() == none
	assert (0.).log() == none
	assert (-1.).log() == none
	assert (2.).asin() == none
	assert (2.).acos() == none
	assert (0.).inverse() == none
	assert (1.).log() == 0
	assert (4.).sqrt() == 2

test "optional Num"
	>> maybe : Num? = none
	assert maybe == none
	assert maybe != 0.0
	assert (2.).sqrt()! * (2.).sqrt()! == 2
	>> present : Num? = 0.5
	assert present! == 0.5
	assert (maybe or 1.5) == 1.5
	assert (present or 1.5) == 0.5

test "exact values as table keys"
	# Mathematically equal values are the same key, whatever shape they have.
	>> t := {0.5: "half", 1./3.: "third"}
	assert t[1./2.]! == "half"
	assert t[(2.).sqrt()! / (2.).sqrt()! / 2]! == "half"
	assert t[1./3.]! == "third"

test "lists of exact values"
	>> xs := [1./3., 0.5, 0.25]
	assert xs.sorted() == [0.25, 1./3., 0.5]
	assert xs.reversed() == [0.25, 0.5, 1./3.]
	assert xs.has(0.5)

test "dividing by zero panics"
	_ := 1.0 / 0.0
fails "division by zero"

test "the modulo of zero panics"
	_ := 1.0 mod 0.0
fails "division by zero"

test "force-unwrapping the square root of a negative panics"
	_ := (-1.).sqrt()!
fails "This was expected to be a value, but it's `none`"
