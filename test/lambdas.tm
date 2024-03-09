>> add_one := func(x:Int) x + 1
>> add_one(10)
= 11

>> shout := func(msg:Text) say("{msg:upper()}!")
>> shout("hello")

>> asdf := add_one
>> asdf(99)
= 100


func make_adder(x:Int)-> func(y:Int)->Int
	return func(y:Int) x + y

>> add_100 := make_adder(100)
>> add_100(5)
= 105


func suffix_fn(fn:func(t:Text)->Text, suffix:Text)->func(t:Text)->Text
	return func(t:Text) fn(t)++suffix

>> shout2 := suffix_fn(Text.upper, "!")
>> shout2("hello")
= "HELLO!"

func mul_func(n:Int, fn:func(x:Int)->Int)-> func(x:Int)->Int
	return func(x:Int) n*fn(x)

>> abs100 := mul_func(100, Int.abs)
>> abs100(-5)
= 500
