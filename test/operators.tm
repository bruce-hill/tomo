# How operators group: precedence, associativity, and how far a leading `-`
# reaches. The snapshots in test/parse pin down the shape of the parse tree;
# these pin down the values it produces.

test "precedence and associativity"
	assert 1 + 2 * 3 - 4 == 3
	assert 2 ^ 3 ^ 2 == 512
	assert 2 ^ 2 * 3 == 12
	assert 10 - 3 - 2 == 5
	assert 100 / 10 / 2 == 5
	assert 1 + 2 < 3 * 4

# Negation sits between `^` and `*`, the same place it sits in ordinary math
# notation, and a literal is no exception: `-2 ^ 2` is `-(2 ^ 2)`.
test "negation"
	x := 3
	assert -x ^ 2 == -9
	assert -2 ^ 2 == -4
	assert (-2) ^ 2 == 4
	assert -2 ^ 2 ^ 3 == -256
	assert 1 - -x ^ 2 == 10
	assert -x * 2 == -6
	assert -x + 1 == -2
	assert 5 - -x == 8
	assert - -1 == 1
	assert -2.5 ^ 2 == -6.25
	assert -(1.0/3) == -1.0/3
	# The formatter prints these back as written: `-` absorbs the `^` but not
	# the `*`, so only the second needs its parentheses (see test-format).
	assert -x^2 == -9
	assert -(x*2) == -6

# A `-` in front of a number still yields a single negative literal, which is
# what lets it be compiled to a sized type that only holds it with the sign on.
test "negative literals"
	small : Int8 = -128
	assert "$small" == "-128"
	assert [1, -2, 3][-2] == -2
	assert -2.5 + 0.5 == -2.0
	assert -1e3 == -1000.0
	assert -0x10 == -16

# Whether the `-` is written against the digits decides only whether the parser
# folds it into the literal; it doesn't decide what the expression means. These
# are all the literal -2, including where only a literal will do, whether
# compiling straight to a sized type or taking its type from the other operand.
#
# Written as `-(2)` rather than `- 2` because the formatter normalizes the spacing
# away, and test-format checks that formatting every .tm file in the tree leaves
# its parse alone. Both representations parse to the same negation of a literal.
test "a `-` written apart from its digits"
	small : Int8 = -(128)
	assert "$small" == "-128"
	assert Int8(-(2)) == Int8(-2)
	assert Int8(-(-(-2))) == Int8(-2)
	assert Int64(-(5)) == Int64(-5)
	assert Float64(-(2)) == Float64(-2)
	assert Int8(1) + -(2) == Int8(-1)
	assert Int8(5) > -(1)
	assert [10, 20, 30][-(1)]! == 30
	# Not just the same value, but the same *type*, which decides whether the
	# arithmetic wraps. Treating `-(1)` as an ordinary Int rather than a literal
	# escalated this to bignum arithmetic, where it came out 128 instead.
	big := Int8(127)
	assert big - -1 == Int8(-128)
	assert big - -(1) == Int8(-128)

# Representations whose value comes out different if the tree is wrong: Euclidean
# `mod` makes `(-x) mod 3` and `-(x mod 3)` disagree, `.abs()` makes `(-x).abs()`
# and `-(x.abs())` disagree, and a suffix binding looser than the `-` would
# change the rest. The trees these check are in test/parse/negation.tm.
test "negation binds where the parse tree says it does"
	x := 5
	n := -3
	xs := [10, 20]
	y := 2.0
	assert -x mod 3 == 1
	assert -n.abs() == -3
	assert -2.abs() == -2
	assert -xs[1]! == -10
	assert -12..round() == -12
	assert 10 - -3 == 13
	assert 10--3 == 13
	assert -y^-1 == -0.5
	assert y^-2.0 == 0.25
	assert -y^2 == -4

# Groupings whose parentheses carry meaning: dropping them regroups the
# expression. test-format checks that formatting every .tm file leaves its parse
# alone, so writing them here is what puts the formatter's parenthesizing under
# test: it prints parentheses exactly where the operator wouldn't absorb the
# operand back.
test "parentheses that have to survive formatting"
	assert (2^3)^2 == 64
	assert 2^(3^2) == 512
	assert (10 - 3) - 2 == 5
	assert 10 - (3 - 2) == 9
	assert (100/10)/2 == 5
	assert 100/(10/2) == 20
	assert (2 + 3)*4 == 20
	assert -(2*3) == -6
