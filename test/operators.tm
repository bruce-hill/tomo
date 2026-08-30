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
	# The formatter prints these back as written -- `-` absorbs the `^` but not
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
# are all the literal -2, including where only a literal will do -- compiling
# straight to a sized type, or taking its type from the other operand.
#
# Spelled `-(2)` rather than `- 2` because the formatter normalizes the spacing
# away, and test-format checks that formatting every .tm file in the tree leaves
# its parse alone. Both spellings parse to the same negation of a literal.
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
