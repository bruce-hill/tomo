
struct Single(x:Int)
struct Pair(x,y:Int)
struct Mixed(x:Int, text:Text)
struct LinkedList(x:Int, next:@LinkedList?=none)
struct Password(text:Text; secret)

struct CorecursiveA(other:@CorecursiveB?)
struct CorecursiveB(other:@CorecursiveA?=none)

func test_literals()
	assert Single(123) == Single(123)
	x := Pair(10, 20)
	assert x == Pair(x=10, y=20)
	y := Pair(y=20, 10)
	assert y == Pair(x=10, y=20)
	assert x == y
	assert x != Pair(-1, -2)

func test_metamethods()
	>> x := Pair(10, 20)
	>> y := Pair(100, 200)
	assert x == y == no
	assert x == Pair(10, 20)
	assert x != Pair(10, 30)

	assert x < Pair(11, 20)
	>> set := {x=yes}
	assert set.has(x) == yes
	assert set.has(y) == no

func test_mixed()
	>> x := Mixed(10, "Hello")
	>> y := Mixed(99, "Hello")
	assert x == y == no
	assert x == Mixed(10, "Hello")
	assert x != Mixed(10, "Bye")
	assert x < Mixed(11, "Hello")
	>> set := {x=yes}
	assert set.has(x) == yes
	assert set.has(y) == no

func test_text()
	>> b := @CorecursiveB()
	>> a := @CorecursiveA(b)
	>> b.other = a
	>> a
	# = @CorecursiveA(@CorecursiveB(@~1))

func main()
	test_literals()
	test_metamethods()
	test_mixed()
	test_text()

	>> @LinkedList(10, @LinkedList(20))

	>> my_pass := Password("Swordfish")
	assert my_pass == Password("Swordfish")
	assert "$my_pass" == "Password(...)"
	>> users_by_password := {my_pass="User1", Password("xxx")="User2"}
	assert "$users_by_password" == '{Password(...)="User1", Password(...)="User2"}'
	assert users_by_password[my_pass]! == "User1"

	>> CorecursiveA(@CorecursiveB())

