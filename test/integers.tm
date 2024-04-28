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

	>> 2 ^ 10
	= 1024 : Num

	>> 3 and 2
	= 2

	>> 3 or 4
	= 7

	>> 3 xor 2
	= 1

	nums := ""
	for x in 5:
		nums ++= "{x},"
	>> nums
	= "1,2,3,4,5,"

	>> x := 123
	>> x:format(digits=5)
	= "00123"
	>> x:hex()
	= "0x7B"
	>> x:octal()
	= "0o173"

	>> Int.random()
	>> Int.min
	= -9223372036854775808
	>> Int.max
	= 9223372036854775807


	>> 123_i32:hex()
	= "0x7B"
	>> 123_i16:hex()
	= "0x7B"
	>> 123_i8:hex()
	= "0x7B"

	>> Int(2.1)
	= 2 : Int
