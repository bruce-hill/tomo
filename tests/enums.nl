enum Foo(Zero, One(x:Int), Two(x,y:Int))

>> Foo__Zero()
>> Foo__One(123)
>> Foo__Two(123, 456)
