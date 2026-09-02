struct Flagged{flag:Bool}


struct Single{x:Int}
struct Pair{x,y:Int}
struct Mixed{x:Int, text:Text}
struct LinkedList{x:Int, next:@LinkedList?=none}
struct Newlines{
    x,y:Int
    name:Text = "default"
    # a comment on its own line
    tag:Text
}
struct Password{text:Text; secret}

struct CorecursiveA{other:@CorecursiveB?}
struct CorecursiveB{other:@CorecursiveA?=none}

test "struct literals"
    >> Single{123}
    assert Single{123} == Single{123}
    >> x := Pair{10, 20}
    assert x == Pair{x=10, y=20}
    >> y := Pair{y=20, 10}
    assert y == Pair{x=10, y=20}
    assert x == y
    assert x != Pair{-1, -2}

test "struct metamethods"
    >> x := Pair{10, 20}
    >> y := Pair{100, 200}
    assert x != y
    assert x == Pair{10, 20}
    assert x != Pair{10, 30}

    >> x < Pair{11, 20}
    assert x < Pair{11, 20}
    >> set := {x: yes}
    >> set.has(x)
    assert set.has(x)
    >> set.has(y)
    assert not set.has(y)

test "mixed struct"
    >> x := Mixed{10, "Hello"}
    >> y := Mixed{99, "Hello"}
    assert x != y
    assert x == Mixed{10, "Hello"}
    assert x != Mixed{10, "Bye"}
    assert x < Mixed{11, "Hello"}
    >> set := {x: yes}
    >> set.has(x)
    assert set.has(x)
    >> set.has(y)
    assert not set.has(y)

test "corecursive struct text"
    >> b := @CorecursiveB{}
    >> a := @CorecursiveA{b}
    >> b.other = a
    >> a

test "linked list"
    >> @LinkedList{10, @LinkedList{20}}

test "secret fields"
    >> my_pass := Password{"Swordfish"}
    assert my_pass == Password{"Swordfish"}
    >> "$my_pass"
    assert "$my_pass" == "Password{...}"
    >> users_by_password := {my_pass: "User1", Password{"xxx"}: "User2"}
    >> "$users_by_password"
    assert "$users_by_password" == '{Password{...}: "User1", Password{...}: "User2"}'
    >> users_by_password[my_pass]
    assert users_by_password[my_pass]! == "User1"

test "corecursive struct construction"
    >> CorecursiveA{@CorecursiveB{}}

test "constructing a struct with the wrong arguments is rejected"
    p := Pair{x=10}
fails_compile "I could not find a constructor matching these arguments for the struct Pair"

test "accessing a nonexistent struct field is rejected"
    p := Pair{10, 20}
    _ := p.z
fails_compile "The field 'z' is not a valid field name of Pair"

test "constructing a struct with parentheses is rejected"
    p := Pair(10, 20)
fails_compile "use curly braces: Pair{...}"

test "struct fields can be separated by newlines instead of commas"
    >> Newlines{1, 2, tag="hi"}
    assert Newlines{1, 2, tag="hi"} == Newlines{x=1, y=2, name="default", tag="hi"}

test "comparisons as struct field values"
    x := 3
    assert Flagged{x == 3}.flag
    assert not Flagged{x == 4}.flag
