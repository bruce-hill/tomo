enum Foo(Zero, One(x:Int), Two(x:Int, y:Int), Three(x:Int, y:Text, z:Bool), Four(x,y,z,w:Int), Last(t:Text))

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

func main()
	>> Foo.Zero
	= Foo.Zero
	>> Foo.One(123)
	= Foo.One(123)
	>> Foo.Two(123, 456)
	= Foo.Two(x=123, y=456)

	>> one := Foo.One(123)
	>> one.One
	= yes
	>> one.Two
	= no

	assert Foo.One(10) == Foo.One(10)

	>> Foo.One(10) == Foo.Zero
	= no

	>> Foo.One(10) == Foo.One(-1)
	= no

	assert Foo.One(10) < Foo.Two(1, 2)

	>> x := Foo.One(123)
	>> t := |x|
	>> t.has(x)
	= yes
	>> t.has(Foo.Zero)
	= no

	>> choose_text(Foo.Zero)
	= "Zero"
	>> choose_text(Foo.One(123))
	= "One: 123"
	>> choose_text(Foo.Two(123, 456))
	= "Two: x=123, y=456"
	>> choose_text(Foo.Three(123, "hi", yes))
	= 'Three: Three(x=123, y="hi", z=yes)'
	>> choose_text(Foo.Four(1,2,3,4))
	= "Four"
	>> choose_text(Foo.Last("XX"))
	= 'else: Last("XX")'

	i := 1
	cases := [Foo.One(1), Foo.One(2), Foo.Zero]
	repeat when cases[i]! is One(x)
		>> x
		i += 1
	else stop

	>> [
		(
			when x is One(y), Two(y,_)
				"Small $y"
			is Zero
				"Zero"
			else
				"Other"
		) for x in [Foo.Zero, Foo.One(1), Foo.Two(2,2), Foo.Three(3,"",no)]
	]
	= ["Zero", "Small 1", "Small 2", "Other"]

	>> expr := when cases[1]! is One(y)
		y + 1
	else
		-1
	= 2

	>> enum_arg_function(Zero)
	>> enum_arg_function(Two(2,3))

	do
		e : enum(One, Two) = One
		>> e
		>> e = Two
		>> when e is One
			say("one")
		is Two
			say("two")

	>> enum_return_function(0)
	= Zero
	>> enum_return_function(1)
	= One
	>> enum_return_function(2)
	= Many

	>> EnumFields(A)
	= EnumFields(x=A)

