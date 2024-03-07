>> (+) [10, 20, 30]
= 60

>> (_max_) [3, 5, 2, 1, 4]
= 5

>> (_max_:abs()) [1, -10, 5]
= -10

struct Foo(x,y:Int)
>> (_max_) [Foo(0, 0), Foo(1, 0), Foo(0, 10)]
= Foo(x=1, y=0)
>> (_max_.y) [Foo(0, 0), Foo(1, 0), Foo(0, 10)]
= Foo(x=0, y=10)
>> (_max_.y:abs()) [Foo(0, 0), Foo(1, 0), Foo(0, 10), Foo(0, -999)]
= Foo(x=0, y=-999)
