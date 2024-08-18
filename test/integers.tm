func main():
	>> 2 + 3
	= 5

	>> 2 * 3
	= 6

	>> 2 + 3 * 4
	= 14

	>> 2 * 3 + 4
	= 10

	>> 1i8 + 2i16
	= 3_i16

	>> 1 << 10
	= 1024

	>> 3 and 2
	= 2

	>> 3 or 4
	= 7

	>> 3 xor 2
	= 1

	nums := ""
	for x in 5:
		nums ++= "$x,"
	>> nums
	= "1,2,3,4,5,"

	>> x := 123i64
	>> x:format(digits=5)
	= "00123"
	>> x:hex()
	= "0x7B"
	>> x:octal()
	= "0o173"

	>> Int.random(1, 100)
	>> Int64.min
	= -9223372036854775808_i64
	>> Int64.max
	= 9223372036854775807_i64


	>> 123_i32:hex()
	= "0x7B"
	>> 123_i16:hex()
	= "0x7B"
	>> 123_i8:hex()
	= "0x7B"

	>> Int(2.1)
	= 2 : Int

	do:
		>> small_int := 1
		= 1
		>> max_small_int := 536870911
		= 536870911
		>> max_i64 := 536870912
		= 536870912
		>> super_big := 9999999999999999999999
		= 9999999999999999999999
		>> max_small_int + 1
		= 536870912

		>> max_small_int + max_small_int
		= 1073741822

		>> super_big + 1
		= 10000000000000000000000

	do:
		for in 20:
			>> n := Int.random(-999999, 999999)
			>> d := Int.random(-999, 999)
			//! n=$n, d=$d:
			>> (n/d)*d + (n mod d) == n
			= yes
