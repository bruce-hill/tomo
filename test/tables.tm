func main():
	>> t := {"one":1, "two":2}
	= {"one":1, "two":2}

	>> t["one"]
	= 1 : Int?
	>> t["two"]
	= 2 : Int?
	>> t["???"]
	= NONE : Int?
	>> t["one"]!
	= 1
	>> t["???"] or -1
	= -1

	t_str := ""
	for k,v in t:
		t_str ++= "($k:$v)"
	>> t_str
	= "(one:1)(two:2)"

	>> t.length
	= 2
	>> t.fallback
	= NONE : {Text:Int}?

	>> t.keys
	= ["one", "two"]
	>> t.values
	= [1, 2]

	>> t2 := {"three":3; fallback=t}
	= {"three":3; fallback={"one":1, "two":2}}

	>> t2["one"]
	= 1 : Int?
	>> t2["three"]
	= 3 : Int?
	>> t2["???"]
	= NONE : Int?

	>> t2.length
	= 1
	>> t2.fallback
	= {"one":1, "two":2} : {Text:Int}?

	t2_str := ""
	for k,v in t2:
		t2_str ++= "($k:$v)"
	>> t2_str
	= "(three:3)"

	>> {i:10*i for i in 5}
	= {1:10, 2:20, 3:30, 4:40, 5:50}
	>> {i:10*i for i in 5 if i mod 2 != 0}
	= {1:10, 3:30, 5:50}
	>> {x:10*x for x in y if x > 1 for y in [3, 4, 5] if y < 5}
	= {2:20, 3:30, 4:40}

	>> t3 := @{1:10, 2:20, 3:30}
	>> t3:remove(3)
	>> t3
	= @{1:10, 2:20}

	do:
		>> plain := {1:10, 2:20, 3:30}
		>> plain[2]!
		= 20
		>> plain[2]!
		= 20
		>> plain[456] or -999
		= -999
		>> plain:has(2)
		= yes
		>> plain:has(456)
		= no

		>> fallback := {4:40; fallback=plain}
		>> fallback:has(1)
		= yes
		>> fallback[1] or -999
		= 10

	do:
		>> t4 := {"one": 1}
		>> t4["one"] = 999
		>> t4["two"] = 222
		>> t4
		= {"one":999, "two":222}

