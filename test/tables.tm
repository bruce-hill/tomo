func main():
	>> t := {"one":1, "two":2; default=999}
	= {"one":1, "two":2; default=999}

	>> t["one"]
	= 1
	>> t["two"]
	= 2
	>> t["???"]
	= 999

	t_str := ""
	for k,v in t:
		t_str ++= "({k}:{v})"
	>> t_str
	= "(one:1)(two:2)"

	>> #t
	= 2
	>> t.default
	= @%999?
	>> t.fallback
	= !{Text:Int}

	>> t.keys
	= ["one", "two"]
	>> t.values
	= [1, 2]

	>> t2 := {"three":3; fallback=t}
	= {"three":3; fallback={"one":1, "two":2; default=999}}

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
	= @%{"one":1, "two":2; default=999}?

	t2_str := ""
	for k,v in t2:
		t2_str ++= "({k}:{v})"
	>> t2_str
	= "(three:3)"

	>> {i:10*i for i in 5}
	= {1:10, 2:20, 3:30, 4:40, 5:50}
	>> {i:10*i for i in 5 if i mod 2 != 0}
	= {1:10, 3:30, 5:50}
	>> {x:10*x for x in y if x > 1 for y in [3, 4, 5] if y < 5}
	= {2:20, 3:30, 4:40}

	>> t3 := {1:10, 2:20, 3:30}
	>> t3:remove(3)
	>> t3
	= {1:10, 2:20}
