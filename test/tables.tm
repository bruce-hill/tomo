
>> t := {"one"=>1, "two"=>2; default=999}
= {"one"=>1, "two"=>2; default=999}

>> t["one"]
= 1
>> t["two"]
= 2
>> t["???"]
= 999

t_str := ""
for k,v in t
	t_str ++= "({k}=>{v})"
>> t_str
= "(one=>1)(two=>2)"

>> #t
= 2
>> t.default
= ?(readonly)999
>> t.fallback
= !{Str=>Int}

>> t.keys
= ["one", "two"]
>> t.values
= [1, 2]

>> t2 := {"three"=>3; fallback=t}
= {"three"=>3; fallback={"one"=>1, "two"=>2; default=999}}

>> t2["one"]
= 1
>> t2["three"]
= 3
>> t2["???"]
= 999

>> #t2
= 1
>> t2.default
= !Int
>> t2.fallback
= ?(readonly){"one"=>1, "two"=>2; default=999}

t2_str := ""
for k,v in t2
	t2_str ++= "({k}=>{v})"
>> t2_str
= "(three=>3)"
