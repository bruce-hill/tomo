enum Foo(Zero, One(x:Int), Two(x,y:Int))

>> Foo.Zero()
= Foo.Zero()
>> Foo.One(123)
= Foo.One(x=123)
>> Foo.Two(123, 456)
= Foo.Two(x=123, y=456)

>> Foo.One(10) == Foo.One(10)
= yes

>> Foo.One(10) == Foo.Zero()
= no

>> Foo.One(10) == Foo.One(-1)
= no

>> Foo.One(10) < Foo.Two(1, 2)
= yes

>> x := Foo.One(123)
>> t := {x=>"found"; default="missing"}
>> t[x]
= "found"
>> t[Foo.Zero()]
= "missing"
