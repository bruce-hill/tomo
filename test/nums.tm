func main():
	>> n := 1.5
	= 1.5

	>> n + n
	= 3

	>> n * 2
	= 3

	>> n - n
	= 0

	>> Num.PI
	= 3.141592653589793

	>> Num.PI:format(precision=10)
	= "3.1415926536"

	>> Num.INF
	= inf
	>> Num.INF:isinf()
	= yes

	>> nan := NONE : Num
	= NONE : Num?
	>> nan == nan
	= yes
	>> nan < nan
	= no
	>> nan > nan
	= no
	>> nan != nan
	= no
	>> nan <> nan
	= 0
	>> nan == 0.0
	= no
	>> nan < 0.0
	= yes
	>> nan > 0.0
	= no
	>> nan != 0.0
	= yes
	>> nan <> 0.0
	= -1

	>> nan + 1
	= NONE : Num?

	>> 0./0.
	= NONE : Num?

	>> Num.PI:cos()!:near(-1)
	= yes
	>> Num.PI:sin()!:near(0)
	= yes

	>> Num.INF:near(-Num.INF)
	= no

	>> Num32.sqrt(16)
	= 4 : Num32?

	>> 0.25:mix(10, 20)
	= 12.5
	>> 2.0:mix(10, 20)
	= 30

	>> Num(5)
	= 5 : Num
