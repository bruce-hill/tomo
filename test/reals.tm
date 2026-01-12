func main()
	# Basic arithmetic
	n := 1.5
	assert n == 1.5
	assert n + n == 3.0
	assert n * 2. == 3.0
	assert n - n == 0.0
	assert n / 2. == 0.75

	# Exact rational arithmetic
	third := 1. / 3.
	assert third * 3. == 1.
	assert (third + third + third) == 1.
	
	# Decimal representation
	assert 0.1 + 0.2 == 0.3
	assert Text(0.1 + 0.2) == "0.3"
	
	# Fraction representation
	assert Text(1. / 3.) == "1/3"
	assert Text(5. / 7.) == "5/7"
	
	# Terminating decimals
	assert Text(3. / 4.) == "0.75"
	assert Text(1. / 8.) == "0.125"
	
	# Square root
	assert (4.).sqrt() == 2.
	assert (9.).sqrt() == 3.
	assert (1. / 4.).sqrt() == 1. / 2.
	
	# Symbolic sqrt simplification
	sqrt2 := (2.).sqrt()
	assert (sqrt2 * sqrt2) == 2.
	
	# Rounding
	assert sqrt2.rounded_to(0.01) == 1.41
	assert sqrt2.rounded_to(0.001) == 1.414
	assert (1234.).rounded_to(100.) == 1200.
	assert (1250.).rounded_to(100.) == 1300.
	
	# Trigonometric functions
	assert (0.).sin() == 0.
	assert (0.).cos() == 1.
	assert (0.).tan() == 0.
	
	# Exponential and logarithm
	assert (0.).exp() == 1.
	
	# Absolute value
	assert (-5.).abs() == 5.
	assert (5.).abs() == 5.
	assert (-3. / 4.).abs() == 3. / 4.
	
	# Floor and ceiling
	assert (2.7).floor() == 2.
	assert (2.3).floor() == 2.
	assert (-2.3).floor() == -3.
	assert (2.3).ceil() == 3.
	assert (2.7).ceil() == 3.
	assert (-2.3).ceil() == -2.
	
	# Floor/ceil on rationals
	assert (7. / 3.).floor() == 2.
	assert (7. / 3.).ceil() == 3.
	assert (-7. / 3.).floor() == -3.
	assert (-7. / 3.).ceil() == -2.
	
	# Modulo
	assert 7. mod 3. == 1.
	assert -7. mod 3. == 2.  # Euclidean division
	assert 7. mod -3. == 1.
	assert 2.5 mod 1.0 == 0.5
	
	# Mod1
	assert 3.7 mod1 2. == 1.7
	
	# Mix (linear interpolation)
	assert (0.25).mix(10., 20.) == 12.5
	assert (0.0).mix(10., 20.) == 10.
	assert (1.0).mix(10., 20.) == 20.
	assert (0.5).mix(10., 20.) == 15.
	
	# Is between
	assert (5.).is_between(1., 10.)
	assert (1.).is_between(1., 10.)
	assert (10.).is_between(1., 10.)
	assert (0.).is_between(1., 10.) == no
	assert (11.).is_between(1., 10.) == no
	
	# Clamped
	assert (5.).clamped(1., 10.) == 5.
	assert (0.).clamped(1., 10.) == 1.
	assert (15.).clamped(1., 10.) == 10.
	assert (1.).clamped(1., 10.) == 1.
	assert (10.).clamped(1., 10.) == 10.
	
	# Comparison
	assert 3. > 2.
	assert 2. < 3.
	assert 3. >= 3.
	assert 3. <= 3.
	assert (1. / 3.) < (1. / 2.)
	
	# Negation
	assert -(5.) == -5.
	assert -(1. / 3.) == -1. / 3.
	
	# Power
	assert (2.).power(3.) == 8.
	assert (2.).power(0.) == 1.
	
	# Parsing
	assert Real.parse("123") == 123.
	assert Real.parse("1.5") == 1.5
	assert Real.parse("0.3333333333333333") == 0.3333333333333333
	assert Real.parse("-5") == -5.
	assert Real.parse("-2.5") == -2.5
	
	# None handling
	none_real : Real? = none
	assert none_real == none
	assert Real.parse("not_a_number") == none
	assert Real.parse("") == none
	
	# Large integers
	big := (99999999999999999999999999999999.)
	assert big + 1. == (100000000000000000000000000000000.)
	
	# Mixed operations preserve exactness
	result := 1. / 3. + 2. / 3.
	assert result == 1.
	
	# Symbolic expressions maintain structure
	expr := sqrt2 + 1.
	assert Text(expr) == "(sqrt(2) + 1)"
	
	print("All Real tests passed!")
