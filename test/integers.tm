test "arithmetic and bitwise operators"
	>> 2 + 3
	>> 2 * 3
	>> 2 + 3 * 4
	>> 1 << 10
	>> (3 and 2)
	>> (3 or 4)
	>> (3 xor 2)
	assert 2 + 3 == 5
	assert 2 * 3 == 6
	assert 2 + 3 * 4 == 14
	assert 2 * 3 + 4 == 10
	assert Int8(1) + Int16(2) == Int16(3)
	assert 1 << 10 == 1024
	assert (3 and 2) == 2
	assert (3 or 4) == 7
	assert (3 xor 2) == 1

test "iterating over an integer range"
	>> nums := ""
	for x in 5
		>> x
		nums ++= "$x,"
	>> nums
	assert nums == "1,2,3,4,5,"

test "hex and octal formatting"
	>> x := Int64(123)
	>> x.hex()
	>> x.hex(digits=4)
	>> x.octal()
	>> Int64.min
	>> Int64.max
	assert x.hex() == "0x7B"
	assert x.hex(digits=4) == "0x007B"
	assert x.octal() == "0o173"
	assert Int64.min == Int64(-9223372036854775808)
	assert Int64.max == Int64(9223372036854775807)
	assert Int32(123).hex() == "0x7B"
	assert Int16(123).hex() == "0x7B"
	assert Int8(123).hex() == "0x7B"

test "truncating a float to an int"
	>> Int(2.1, truncate=yes)
	assert Int(2.1, truncate=yes) == 2

test "big integer promotion"
	>> small_int := 1
	assert small_int == 1
	>> max_small_int := 536870911
	assert max_small_int == 536870911
	>> max_i64 := 536870912
	assert max_i64 == 536870912
	>> super_big := 9999999999999999999999
	assert super_big == 9999999999999999999999
	>> max_small_int + 1
	>> max_small_int + max_small_int
	>> super_big + 1
	assert max_small_int + 1 == 536870912
	assert max_small_int + max_small_int == 1073741822
	assert super_big + 1 == 10000000000000000000000

test "division and modulo identity"
	>> interesting_numerators := [-999999, -100, -23, -1, 0, 1, 23, 100, 999999]
	>> interesting_denominators := [-99, -20, -17, -1, 1, 17, 20, 99]
	for n in interesting_numerators
		for d in interesting_denominators
			assert (n/d)*d + (n mod d) == n

test "primes"
	>> 0.next_prime()
	>> 7.next_prime()
	assert 0.next_prime() == 2
	assert 7.next_prime() == 11
	assert (and: p.is_prime() for p in [
		2, 3, 5, 7,
		137372146048179869781170214707,
		811418847921670560768224995279,
		292590241572454328697048860273,
		754893741683930091960170890717,
		319651808258437169510475301537,
		323890224935694708770556249787,
		507626552342376235511933571091,
		548605069630614185274710840981,
		121475876690852432982324195553,
		771958616175795150904761471637,
	])!

	assert (or: p.is_prime() for p in [
		-1, 0, 1, 4, 6,
		137372146048179869781170214707*2,
		811418847921670560768224995279*3,
		292590241572454328697048860273*754893741683930091960170890717,
	])! == no

test "converting booleans to ints"
	>> Int(yes)
	>> Int(no)
	>> Int64(yes)
	>> Int64(no)
	assert Int(yes) == 1
	assert Int(no) == 0
	assert Int64(yes) == Int64(1)
	assert Int64(no) == Int64(0)

test "choose and factorial"
	>> 4.choose(2)
	>> 4.factorial()
	assert 4.choose(2) == 6
	assert 4.factorial() == 24

test "is_between"
	>> 3.is_between(1, 5)
	>> 3.is_between(1, 3)
	>> 3.is_between(5, 1)
	>> 3.is_between(100, 200)
	assert 3.is_between(1, 5)
	assert 3.is_between(1, 3)
	assert 3.is_between(5, 1)
	assert not 3.is_between(100, 200)

test "get_bit"
	>> 6.get_bit(1)
	>> 6.get_bit(2)
	>> 6.get_bit(3)
	>> 6.get_bit(4)
	assert not 6.get_bit(1)
	assert 6.get_bit(2)
	assert 6.get_bit(3)
	assert not 6.get_bit(4)
	assert not Int64(6).get_bit(1)
	assert Int64(6).get_bit(2)
	assert Int64(6).get_bit(3)
	assert not Int64(6).get_bit(4)

test "parsing integers"
	>> Int.parse("123")
	>> Int.parse("0x10")
	>> Int.parse("0o10")
	>> Int.parse("0b10")
	>> Int.parse("abc")
	>> Int.parse("-123")
	assert Int.parse("123") == 123
	assert Int.parse("0x10") == 16
	assert Int.parse("0o10") == 8
	assert Int.parse("0b10") == 2
	assert Int.parse("abc") == none
	assert Int.parse("-123") == -123
	assert Int.parse("-0x10") == -16
	assert Int.parse("-0o10") == -8
	assert Int.parse("-0b10") == -2
	for base in 2.to(36)
		assert Int.parse("10", base=base) == base
	assert Int.parse("111", base=1) == 3
	assert Int.parse("z", base=36) == 35
	assert Int.parse("Z", base=36) == 35
	assert Int.parse("-z", base=36) == -35
	assert Int.parse("-Z", base=36) == -35

test "integers that don't fit a fixed-width type are rejected"
	x := Int8(99999)
fails_compile "This integer cannot fit in a 8-bit value"

test "arithmetic between an integer and text is rejected"
	x := 5 + "hello"
fails_compile "I don't know how to do math operations between Int and Text"

test "dividing an integer by zero panics"
	x := 10
	y := 0
	_ := x / y
fails "Cannot divide 10 by zero"

test "integer modulo by zero panics"
	x := 10
	y := 0
	_ := x mod y
fails "Cannot take 10 modulo zero"

test "dividing a fixed-width integer by zero panics"
	x := Int64(10)
	y := Int64(0)
	_ := x / y
fails "Cannot divide 10 by zero"

test "dividing a byte by zero panics"
	x := Byte(10)
	y := Byte(0)
	_ := x / y
fails "Cannot divide 10 by zero"

test "compound divide-assignment by zero panics"
	x := 10
	x /= 0
fails "Cannot divide 10 by zero"
