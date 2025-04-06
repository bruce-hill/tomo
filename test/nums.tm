func main():
	>> n := 1.5
	= 1.5

	>> n + n
	= 3.

	>> n * 2
	= 3.

	>> n - n
	= 0.

	>> Num.PI
	= 3.141592653589793

	>> Num.PI.format(precision=10)
	= "3.1415926536"

	>> Num.INF
	= Num.INF
	>> Num.INF.isinf()
	= yes

	>> none_num : Num? = none
	= none
	>> none_num == none_num
	= yes
	>> none_num < none_num
	= no
	>> none_num > none_num
	= no
	>> none_num != none_num
	= no
	>> none_num <> none_num
	= Int32(0)
	>> none_num == 0.0
	= no
	>> none_num < 0.0
	= yes
	>> none_num > 0.0
	= no
	>> none_num != 0.0
	= yes
	>> none_num <> 0.0
	= Int32(-1)

	# >> nan + 1
	# = none

	>> 0./0.

	# >> 0./0.
	# = none

	>> Num.PI.cos()!.near(-1)
	= yes
	>> Num.PI.sin()!.near(0)
	= yes

	>> Num.INF.near(-Num.INF)
	= no

	>> Num32.sqrt(16)
	= Num32(4)?

	>> (0.25).mix(10, 20)
	= 12.5
	>> (2.0).mix(10, 20)
	= 30.

	>> Num(5)
	= 5.

	>> (0.5).percent()
	= "50%"
