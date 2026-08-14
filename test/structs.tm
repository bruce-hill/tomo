
struct Single(x:Int)
struct Pair(x,y:Int)
struct Mixed(x:Int, text:Text)
struct LinkedList(x:Int, next:@LinkedList?=none)
struct Password(text:Text; secret)

struct CorecursiveA(other:@CorecursiveB?)
struct CorecursiveB(other:@CorecursiveA?=none)

test "struct literals"
	>> Single(123)
	assert Single(123) == Single(123)
	>> x := Pair(10, 20)
	assert x == Pair(x=10, y=20)
	>> y := Pair(y=20, 10)
	assert y == Pair(x=10, y=20)
	assert x == y
	assert x != Pair(-1, -2)

test "struct metamethods"
	>> x := Pair(10, 20)
	>> y := Pair(100, 200)
	>> x == y
	assert x == y == no
	assert x == Pair(10, 20)
	assert x != Pair(10, 30)

	>> x < Pair(11, 20)
	assert x < Pair(11, 20)
	>> set := {x: yes}
	>> set.has(x)
	assert set.has(x) == yes
	>> set.has(y)
	assert set.has(y) == no

test "mixed struct"
	>> x := Mixed(10, "Hello")
	>> y := Mixed(99, "Hello")
	>> x == y
	assert x == y == no
	assert x == Mixed(10, "Hello")
	assert x != Mixed(10, "Bye")
	assert x < Mixed(11, "Hello")
	>> set := {x: yes}
	>> set.has(x)
	assert set.has(x) == yes
	>> set.has(y)
	assert set.has(y) == no

test "corecursive struct text"
	>> b := @CorecursiveB()
	>> a := @CorecursiveA(b)
	>> b.other = a
	>> a

test "linked list"
	>> @LinkedList(10, @LinkedList(20))

test "secret fields"
	>> my_pass := Password("Swordfish")
	assert my_pass == Password("Swordfish")
	>> "$my_pass"
	assert "$my_pass" == "Password(...)"
	>> users_by_password := {my_pass: "User1", Password("xxx"): "User2"}
	>> "$users_by_password"
	assert "$users_by_password" == '{Password(...): "User1", Password(...): "User2"}'
	>> users_by_password[my_pass]
	assert users_by_password[my_pass]! == "User1"

test "corecursive struct construction"
	>> CorecursiveA(@CorecursiveB())
