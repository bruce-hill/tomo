func main()
	assert 2 + 3 == 5

	assert 2 * 3 == 6

	assert 2 + 3 * 4 == 14

	assert 2 * 3 + 4 == 10

	assert Int8(1) + Int16(2) == Int16(3)

	assert 1 << 10 == 1024

	# say("Signed and unsigned bit shifting:")
	# >> Int64(-2) << 1
	# = Int64(-4)
	# >> Int64(-2) <<< 1
	# = Int64(-4)
	# >> Int64(-2) >> 1
	# = Int64(-1)
	# >> Int64(-2) >>> 1
	# = Int64(9223372036854775807)

	assert (3 and 2) == 2

	assert (3 or 4) == 7

	assert (3 xor 2) == 1

	nums := ""
	for x in 5
		nums ++= "$x,"
	assert nums == "1,2,3,4,5,"

	>> x := Int64(123)
	assert x.hex() == "0x7B"
	assert x.hex(digits=4) == "0x007B"
	assert x.octal() == "0o173"

	assert Int64.min == Int64(-9223372036854775808)
	assert Int64.max == Int64(9223372036854775807)


	assert Int32(123).hex() == "0x7B"
	assert Int16(123).hex() == "0x7B"
	assert Int8(123).hex() == "0x7B"

	assert Int(2.1, truncate=yes) == 2

	do
		small_int := 1
		assert small_int == 1
		max_small_int := 536870911
		assert max_small_int == 536870911
		max_i64 := 536870912
		assert max_i64 == 536870912
		super_big := 9999999999999999999999
		assert super_big == 9999999999999999999999
		assert max_small_int + 1 == 536870912

		assert max_small_int + max_small_int == 1073741822

		assert super_big + 1 == 10000000000000000000000

	do
		interesting_numerators := [-999999, -100, -23, -1, 0, 1, 23, 100, 999999]
		interesting_denominators := [-99, -20, -17, -1, 1, 17, 20, 99]
		for n in interesting_numerators
			for d in interesting_denominators
				assert (n/d)*d + (n mod d) == n

	assert (0).next_prime() == 2
	assert (7).next_prime() == 11
	#>> (11).prev_prime()
	#= 7
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

	assert Int(yes) == 1
	assert Int(no) == 0

	assert Int64(yes) == Int64(1)
	assert Int64(no) == Int64(0)

	assert (4).choose(2) == 6

	assert (4).factorial() == 24

	assert (3).is_between(1, 5) == yes
	assert (3).is_between(1, 3) == yes
	assert (3).is_between(100, 200) == no

	assert (6).get_bit(1) == no
	assert (6).get_bit(2) == yes
	assert (6).get_bit(3) == yes
	assert (6).get_bit(4) == no

	assert Int64(6).get_bit(1) == no
	assert Int64(6).get_bit(2) == yes
	assert Int64(6).get_bit(3) == yes
	assert Int64(6).get_bit(4) == no
