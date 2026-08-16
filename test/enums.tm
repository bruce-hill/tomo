enum Foo(Zero, One(x:Int), Two(x:Int, y:Int), Three(x:Int, y:Text, z:Bool), Four(x,y,z,w:Int), Last(t:Text))
enum OnlyTags(A, B, C, D)

func choose_text(f:Foo->Text)
	>> f
	when f is Zero
		return "Zero"
	is One(one)
		return "One: $one"
	is Two(x, y)
		return "Two: x=$x, y=$y"
	is Three(three)
		return "Three: $three"
	is Four
		return "Four"
	else
		return "else: $f"

func enum_arg_function(thing:enum(Zero, One(x:Int), Two(x,y:Int)))
	>> thing

func enum_return_function(i:Int -> enum(Zero, One, Many))
	if i == 0
		return Zero
	else if i == 1
		return One
	else
		return Many

struct EnumFields(x:enum(A, B, C))

test "enum equality and comparison"
	>> Foo.Zero
	>> Foo.One(123)
	>> Foo.Two(123, 456)
	>> Foo.One(10)
	assert Foo.Zero == Foo.Zero
	assert Foo.One(123) == Foo.One(123)
	assert Foo.Two(123, 456) == Foo.Two(x=123, y=456)
	assert Foo.One(10) == Foo.One(10)
	assert Foo.One(10) == Foo.Zero == no
	assert Foo.One(10) == Foo.One(-1) == no
	assert Foo.One(10) < Foo.Two(1, 2)

test "enum values as table keys"
	>> x := Foo.One(123)
	>> t := {x: yes}
	>> t.has(x)
	>> t.has(Foo.Zero)
	assert t.has(x) == yes
	assert t.has(Foo.Zero) == no

test "pattern matching with when"
	>> choose_text(Foo.Zero)
	>> choose_text(Foo.One(123))
	>> choose_text(Foo.Two(123, 456))
	>> choose_text(Foo.Four(1,2,3,4))
	assert choose_text(Foo.Zero) == "Zero"
	assert choose_text(Foo.One(123)) == "One: 123"
	assert choose_text(Foo.Two(123, 456)) == "Two: x=123, y=456"
	assert choose_text(Foo.Three(123, "hi", yes)) == 'Three: Three(x=123, y="hi", z=yes)'
	assert choose_text(Foo.Four(1,2,3,4)) == "Four"
	assert choose_text(Foo.Last("XX")) == 'else: Last("XX")'

test "repeat when over a list"
	>> i := 1
	>> cases := [Foo.One(1), Foo.One(2), Foo.Zero]
	repeat when cases[i]! is One(x)
		>> x
		i += 1
	else stop

test "when in a comprehension"
	assert [
		(
			when x is One(y), Two(y,_)
				"Small $y"
			is Zero
				"Zero"
			else
				"Other"
		) for x in [Foo.Zero, Foo.One(1), Foo.Two(2,2), Foo.Three(3,"",no)]
	] == ["Zero", "Small 1", "Small 2", "Other"]

test "when as an expression"
	>> cases := [Foo.One(1), Foo.One(2), Foo.Zero]
	>> expr := when cases[1]! is One(y)
		y + 1
	else
		-1
	>> expr
	assert expr == 2

test "inline enum argument type"
	>> enum_arg_function(Zero)
	>> enum_arg_function(Two(2,3))

test "inline enum local variable"
	e : enum(One, Two) = One
	e = Two
	when e is One
		say("one")
	is Two
		say("two")

test "inline enum return type"
	>> enum_return_function(0)
	>> enum_return_function(1)
	>> enum_return_function(2)
	assert enum_return_function(0) == Zero
	assert enum_return_function(1) == One
	assert enum_return_function(2) == Many

test "struct with enum field"
	>> EnumFields(A)
	>> EnumFields(x=A)
	assert EnumFields(A) == EnumFields(x=A)

test "tag accessors on a tag-only enum"
	>> e := OnlyTags.A
	>> e.A
	>> e.B
	assert e.A == OnlyTags.A.A
	assert e.B == none

test "tag accessors"
	>> e := Foo.Zero
	>> e.Zero
	>> e.One
	>> e.Two
	assert e.Zero == Foo.Zero.Zero
	assert e.One == none
	assert e.Two == none
	>> ep := @Foo.Zero
	>> ep.Zero
	assert ep.Zero == Foo.Zero.Zero
	assert ep.One == none
	assert ep.Two == none

test "tag accessor extraction"
	>> e := Foo.Two(123, 456)
	>> e.Zero
	>> e.One
	>> e.Two
	assert e.Zero == none
	assert e.One == none
	assert e.Two != none
	>> ep := Foo.Two(123, 456)
	assert ep.Zero == none
	assert ep.One == none
	assert ep.Two != none
	>> two := e.Two!
	when e is Two(x,y)
		assert two.x == x
		assert two.y == y
	else fail("Unreachable")

test "referencing a nonexistent enum variant is rejected"
	f := Foo.Nonexistent
fails_compile "I couldn't find the field 'Nonexistent' on this type"

test "constructing an enum variant with the wrong argument type is rejected"
	f := Foo.One("not an int")
fails_compile "This function's signature doesn't match this call site."
