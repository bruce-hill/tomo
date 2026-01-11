func main()
	# Basic arithmetic
	n := Real(1.5)
	assert n == 1.5
	assert n + n == 3.0
	assert n * 2 == 3.0
	assert n - n == 0.0
	assert n / 2 == 0.75

	# Exact rational arithmetic
	third := Real(1) / Real(3)
	assert third * 3 == 1
	assert (third + third + third) == 1
	
	# Decimal representation
	assert Real(0.1) + Real(0.2) == Real(0.3)
	assert (Real(0.1) + Real(0.2)).value_as_text() == "0.3"
	
	# Fraction representation
	assert (Real(1) / Real(3)).value_as_text() == "1/3"
	assert (Real(5) / Real(7)).value_as_text() == "5/7"
	
	# Terminating decimals
	assert (Real(3) / Real(4)).value_as_text() == "0.75"
	assert (Real(1) / Real(8)).value_as_text() == "0.125"
	
	# Square root
	assert Real(4).sqrt() == 2
	assert Real(9).sqrt() == 3
	assert (Real(1) / Real(4)).sqrt() == Real(1) / Real(2)
	
	# Symbolic sqrt simplification
	sqrt2 := Real(2).sqrt()
	assert (sqrt2 * sqrt2) == 2
	
	# Rounding
	assert sqrt2.rounded_to(0.01) == 1.41
	assert sqrt2.rounded_to(0.001) == 1.414
	assert Real(1234).rounded_to(100) == 1200
	assert Real(1250).rounded_to(100) == 1300
	
	# Trigonometric functions
	assert Real(0).sin().value_as_text() == "0"
	assert Real(0).cos().value_as_text() == "1"
	assert Real(0).tan().value_as_text() == "0"
	
	# Exponential and logarithm
	assert Real(0).exp() == 1
	
	# Absolute value
	assert Real(-5).abs() == 5
	assert Real(5).abs() == 5
	assert (Real(-3) / Real(4)).abs() == Real(3) / Real(4)
	
	# Floor and ceiling
	assert Real(2.7).floor() == 2
	assert Real(2.3).floor() == 2
	assert Real(-2.3).floor() == -3
	assert Real(2.3).ceil() == 3
	assert Real(2.7).ceil() == 3
	assert Real(-2.3).ceil() == -2
	
	# Floor/ceil on rationals
	assert (Real(7) / Real(3)).floor() == 2
	assert (Real(7) / Real(3)).ceil() == 3
	assert (Real(-7) / Real(3)).floor() == -3
	assert (Real(-7) / Real(3)).ceil() == -2
	
	# Modulo
	assert Real(7).mod(3) == 1
	assert Real(-7).mod(3) == 2  # Euclidean division
	assert Real(7).mod(-3) == -2
	assert Real(2.5).mod(1.0) == 0.5
	
	# Mod1
	assert Real(3.7).mod1(2) == 1.7
	
	# Mix (linear interpolation)
	assert Real(0.25).mix(10, 20) == 12.5
	assert Real(0.0).mix(10, 20) == 10
	assert Real(1.0).mix(10, 20) == 20
	assert Real(0.5).mix(10, 20) == 15
	
	# Is between
	assert Real(5).is_between(1, 10)
	assert Real(1).is_between(1, 10)
	assert Real(10).is_between(1, 10)
	assert Real(0).is_between(1, 10) == no
	assert Real(11).is_between(1, 10) == no
	
	# Clamped
	assert Real(5).clamped(1, 10) == 5
	assert Real(0).clamped(1, 10) == 1
	assert Real(15).clamped(1, 10) == 10
	assert Real(1).clamped(1, 10) == 1
	assert Real(10).clamped(1, 10) == 10
	
	# Comparison
	assert Real(3) > Real(2)
	assert Real(2) < Real(3)
	assert Real(3) >= Real(3)
	assert Real(3) <= Real(3)
	assert (Real(1) / Real(3)) < (Real(1) / Real(2))
	
	# Negation
	assert -Real(5) == Real(-5)
	assert -(Real(1) / Real(3)) == Real(-1) / Real(3)
	
	# Power
	assert Real(2).power(3) == 8
	assert Real(2).power(0) == 1
	
	# Parsing
	assert Real.parse("123") == 123
	assert Real.parse("1.5") == 1.5
	assert Real.parse("0.3333333333333333") == Real(0.3333333333333333)
	assert Real.parse("-5") == -5
	assert Real.parse("-2.5") == -2.5
	
	# None handling
	none_real : Real? = none
	assert none_real == none
	assert Real.parse("not_a_number") == none
	assert Real.parse("") == none
	
	# Large integers
	big := Real(9999999999999999)
	assert big + 1 == Real(10000000000000000)
	
	# Mixed operations preserve exactness
	result := Real(1) / Real(3) + Real(2) / Real(3)
	assert result == 1
	
	# Symbolic expressions maintain structure
	expr := sqrt2 + Real(1)
	assert expr.value_as_text().contains("sqrt")
	
	print("All Real tests passed!")
