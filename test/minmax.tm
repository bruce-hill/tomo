
>> 3 _min_ 5
= 3
>> 5 _min_ 3
= 3

struct Foo(x:Int, y:Int)
	func len(f:Foo)->Num
		return Num.sqrt(f.x*f.x + f.y*f.y)

>> Foo(5, 1) _min_ Foo(5, 999)
= Foo(x=5, y=1)

>> Foo(5, 999) _min_.x Foo(5, 1)
= Foo(x=5, y=999)

>> Foo(999, 1) _min_.y Foo(1, 10)
= Foo(x=999, y=1)

>> Foo(-999, -999) _max_:len() Foo(10, 10)
= Foo(x=-999, y=-999)


>> foos := [Foo(5, 1), Foo(5, 99), Foo(-999, -999)]
>> (_max_) foos
= Foo(x=5, y=99)
