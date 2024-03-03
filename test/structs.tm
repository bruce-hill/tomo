
struct Pair(x,y:Int)
struct Mixed(x:Int, text:Text)

func test_literals()
	>> x := Pair(10, 20)
	= Pair(x=10, y=20)
	>> y := Pair(y=20, 10)
	= Pair(x=10, y=20)
	>> x == y
	= yes
	>> x == Pair(-1, -2)
	= no
test_literals()

func test_metamethods()
	>> x := Pair(10, 20)
	>> y := Pair(100, 200)
	>> x == y
	= no
	>> x == Pair(10, 20)
	= yes
	>> x == Pair(10, 30)
	= no

	>> x < Pair(11, 20)
	= yes
	>> t2 := {x=> "found"; default="missing"}
	>> t2[x]
	= "found"
	>> t2[y]
	= "missing"
test_metamethods()

func test_mixed()
	>> x := Mixed(10, "Hello")
	>> y := Mixed(99, "Hello")
	>> x == y
	= no
	>> x == Mixed(10, "Hello")
	= yes
	>> x == Mixed(10, "Bye")
	= no
	>> x < Mixed(11, "Hello")
	= yes
	>> t := {x=> "found"; default="missing"}
	>> t[x]
	= "found"
	>> t[y]
	= "missing"
test_mixed()

