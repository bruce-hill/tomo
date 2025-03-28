struct Foo(x,y:Int)

func main():
	>> (+: [10, 20, 30])
	= 60?

	>> (+: [:Int])
	= none : Int

	>> (+: [10, 20, 30]) or 0
	= 60

	>> (+: [:Int]) or 0
	= 0

	>> (_max_: [3, 5, 2, 1, 4])
	= 5?

	>> (_max_:abs(): [1, -10, 5])
	= -10?

	>> (_max_: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])!
	= Foo(x=1, y=0)
	>> (_max_.y: [Foo(0, 0), Foo(1, 0), Foo(0, 10)])!
	= Foo(x=0, y=10)
	>> (_max_.y:abs(): [Foo(0, 0), Foo(1, 0), Foo(0, 10), Foo(0, -999)])!
	= Foo(x=0, y=-999)

	!! (or) and (and) have early out behavior:
	>> (or: i == 3 for i in 9999999999999999999999999999)!
	= yes

	>> (and: i < 10 for i in 9999999999999999999999999999)!
	= no

	>> (<=: [1, 2, 2, 3, 4])!
	= yes

	>> (<=: [:Int])
	= none : Bool

	>> (<=: [5, 4, 3, 2, 1])!
	= no
