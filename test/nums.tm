test "numeric literals are exact"
	>> 0.1 + 0.2
	assert 0.1 + 0.2 == 0.3
	assert 0.1 + 0.2 - 0.3 == 0
	assert 1.1 * 3 == 3.3
	assert 3.15 == 63/20.

test "integer division with `/` is exact"
	>> 1/3
	assert 1/3 == 1./3.
	assert 7/2 == 3.5
	assert (1/3) * 3 == 1
	assert Int64(1) / Int64(3) == 1/3
	# The integer quotient is spelled `//`:
	assert 7//2 == 3
	assert 7//2 + 7 mod 2 * 0 == 3

test "Euclidean division and modulus on Num"
	# Same invariants as the integer types: x == y*(x//y) + (x mod y), with
	# the remainder always non-negative.
	assert 7.5 // 2 == 3
	assert 7.5 mod 2 == 1.5
	assert -7.5 // 2 == -4
	assert -7.5 mod 2 == 0.5
	assert 7.5 // -2 == -3
	assert 7.5 mod -2 == 1.5
	for pair in [[7.5, 2.], [-7.5, 2.], [7.5, -2.], [-7.5, -2.], [1./3., 0.25]]
		x := pair[1]!
		y := pair[2]!
		assert x == y * (x // y) + (x mod y)
		assert (x mod y) >= 0

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

test "converting to a float approximates by default"
	>> Float64(1./3.)
	assert Float64(0.5) == 0.5
	assert Float64(1./3.) == Float64(1) / Float64(3)
	assert Float32(0.5) == Float32(0.5)
	# A float that happens to be exact converts under truncate=no too:
	assert Float64(0.5, truncate=no) == 0.5

test "converting to an integer is strict by default"
	>> Int(4.)
	assert Int(4.) == 4
	assert Int(4.9, truncate=yes) == 4
	assert Int(-4.9, truncate=yes) == -4
	assert Int((2.).power(100)) == 1267650600228229401496703205376
	assert Int64(4.) == 4
	assert Byte(255.) == Byte(0xff)
	assert Int((4.5).floor()) == 4
	assert Int((4.5).ceil()) == 5

test "constructing a Num is always exact"
	assert Num(5) == 5
	assert Num(Int64(5)) == 5
	assert Num(Float64(0.5)) == 0.5
	assert Num(yes) == 1
	# Float64(0.1) is the double, not a tenth -- so this is not 0.1:
	assert Num(Float64(0.1)) != 0.1

test "parsing"
	assert Num.parse("1.5")! == 1.5
	assert Num.parse("0.1")! == 0.1
	assert Num.parse("22/7")! == 22./7.
	assert Num.parse("nope") == none

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

test "identical expressions are equal"
	# Two separately built irrationals with the same structure are the same
	# value, and need no numeric refinement to say so.
	assert (2.).sin() == (2.).sin()
	assert (2.).sin() + Num.PI == (2.).sin() + Num.PI
	assert (2.).sin() * Num.PI == (2.).sin() * Num.PI
	assert (2.).sin() != (3.).sin()

test "equal values are equal however they're written"
	assert (2.).sqrt()! == (8.).sqrt()! / 2
	assert (2.).sqrt()! * (2.).sqrt()! == 2
	assert Num.PI * 2 == Num.TAU
	# Proving this symbolically takes reasoning no engine does in full, so it
	# falls back to agreeing to 40 digits -- which it does.
	assert (3. + 2 * (2.).sqrt()!).sqrt()! == 1 + (2.).sqrt()!

test "irrationals work as table keys"
	>> t := {(2.).sin(): "sin 2", Num.PI: "pi", (2.).sqrt()!: "root 2"}
	assert t[(2.).sin()]! == "sin 2"
	assert t[Num.PI]! == "pi"
	assert t[(8.).sqrt()! / 2]! == "root 2"

test "a heavily shared expression stays small"
	# Doubling an irrational builds a DAG whose unfolded form is 2^n; the
	# symbolic form names repeated subexpressions instead of copying them.
	x := (1.).sin()
	for i in 1.to(30)
		x = x + x
	assert x.symbolic().length < 1000
	bytes : [Byte] = x
	roundtrip : Num = bytes
	assert roundtrip == x

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

test "converting a fraction to an Int without truncating panics"
	_ := Int(1./3.)
fails "Could not convert this Num to an Int without truncation"

test "converting a fraction to a Float64 without truncating panics"
	_ := Float64(1./3., truncate=no)
fails "Could not convert this Num to a Float64 without losing precision"

test "serialization round-trips exact rationals"
	>> obj := [0.5, 1./3., 0.1 + 0.2, (2.).power(100), -7./2., 0.]
	>> bytes : [Byte] = obj
	>> roundtrip : [Num] = bytes
	assert roundtrip == obj

test "serialization round-trips irrationals"
	irrationals := [(2.).sqrt()!, Num.PI, Num.PI * 2, (2.).sin(), (2.).exp(), (3.).log()!, Num.PI + (2.).sqrt()!, Num.PI * Num.PI * Num.PI, (1. + Num.PI) * (2.).sin(), (1.).atan()]
	for x in irrationals
		bytes : [Byte] = x
		roundtrip : Num = bytes
		assert roundtrip.symbolic() == x.symbolic()
		assert roundtrip == x

test "dividing by zero panics"
	_ := 1.0 / 0.0
fails "division by zero"

test "the modulo of zero panics"
	_ := 1.0 mod 0.0
fails "division by zero"

test "force-unwrapping the square root of a negative panics"
	_ := (-1.).sqrt()!
fails "This was expected to be a value, but it's `none`"
