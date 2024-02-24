enum Foo(Zero, One(x:Int), Two(x,y:Int))

>> Foo__Zero()
= Foo.Zero()
>> Foo__One(123)
= Foo.One(x=123)
>> Foo__Two(123, 456)
= Foo.Two(x=123, y=456)

>> Foo__One(10) == Foo__One(10)
= yes

>> Foo__One(10) == Foo__Zero()
= no

>> Foo__One(10) == Foo__One(-1)
= no

>> Foo__One(10) < Foo__Two(1, 2)
= yes

>> x := Foo__One(123)
>> t := {x=>"found"; default="missing"}
>> t[x]
= "found"
>> t[Foo__Zero()]
= "missing"
