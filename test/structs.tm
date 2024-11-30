
struct Single(x:Int)
struct Pair(x,y:Int)
struct Mixed(x:Int, text:Text)
struct LinkedList(x:Int, next=NONE:@LinkedList)
struct Password(text:Text; secret)

struct CorecursiveA(other:@CorecursiveB?)
struct CorecursiveB(other=NONE:@CorecursiveA)

func test_literals():
	>> Single(123)
	= Single(123)
	>> x := Pair(10, 20)
	= Pair(x=10, y=20)
	>> y := Pair(y=20, 10)
	= Pair(x=10, y=20)
	>> x == y
	= yes
	>> x == Pair(-1, -2)
	= no

func test_metamethods():
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
	>> set := {x}
	>> set:has(x)
	= yes
	>> set:has(y)
	= no

func test_mixed():
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
	>> set := {x}
	>> set:has(x)
	= yes
	>> set:has(y)
	= no

func test_text():
	>> b := @CorecursiveB()
	>> a := @CorecursiveA(b)
	>> b.other = a
	>> a
	= @CorecursiveA(@CorecursiveB(@~1))

func main():
	test_literals()
	test_metamethods()
	test_mixed()
	test_text()

	>> @LinkedList(10, @LinkedList(20))

	>> my_pass := Password("Swordfish")
	= Password(...)
	>> users_by_password := {my_pass:"User1", Password("xxx"):"User2"}
	= {Password(...):"User1", Password(...):"User2"}
	>> users_by_password[my_pass]!
	= "User1"

	>> CorecursiveA(@CorecursiveB())

