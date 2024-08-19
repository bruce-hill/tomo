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
	= 3.14159

	>> Num.PI:format(precision=10)
	= "3.1415926536"

	>> Num.random()

	>> Num.INF
	= inf
	>> Num.INF:isinf()
	= yes

	>> Num.nan()
	= nan
	>> nan := Num.nan()
	>> nan:isnan()
	= yes
	>> nan == nan
	= no

	>> Num.PI:cos():near(-1)
	= yes
	>> Num.PI:sin():near(0)
	= yes

	>> Num.nan():near(Num.nan())
	= no

	>> Num.INF:near(-Num.INF)
	= no

	>> Num32.sqrt(16f32)
	= 4_f32

	>> 0.25:mix(10, 20)
	= 12.5
	>> 2.0:mix(10, 20)
	= 30

	>> Num(5)
	= 5 : Num
