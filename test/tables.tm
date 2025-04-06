func main():
	>> t := {"one"=1, "two"=2}
	= {"one"=1, "two"=2}

	>> t["one"]
	= 1?
	>> t["two"]
	= 2?
	>> t["???"]
	= none
	>> t["one"]!
	= 1
	>> t["???"] or -1
	= -1

	t_str := ""
	for k,v in t:
		t_str ++= "($k=$v)"
	>> t_str
	= "(one=1)(two=2)"

	>> t.length
	= 2
	>> t.fallback
	= none

	>> t.keys
	= ["one", "two"]
	>> t.values
	= [1, 2]

	>> t2 := {"three"=3; fallback=t}
	= {"three"=3; fallback={"one"=1, "two"=2}}

	>> t2["one"]
	= 1?
	>> t2["three"]
	= 3?
	>> t2["???"]
	= none

	>> t2.length
	= 1
	>> t2.fallback
	= {"one"=1, "two"=2}?

	t2_str := ""
	for k,v in t2:
		t2_str ++= "($k=$v)"
	>> t2_str
	= "(three=3)"

	>> {i=10*i for i in 5}
	= {1=10, 2=20, 3=30, 4=40, 5=50}
	>> {i=10*i for i in 5 if i mod 2 != 0}
	= {1=10, 3=30, 5=50}
	>> {x=10*x for x in y if x > 1 for y in [3, 4, 5] if y < 5}
	= {2=20, 3=30, 4=40}

	>> t3 := @{1=10, 2=20, 3=30}
	>> t3.remove(3)
	>> t3
	= @{1=10, 2=20}

	do:
		>> plain := {1=10, 2=20, 3=30}
		>> plain[2]!
		= 20
		>> plain[2]!
		= 20
		>> plain[456] or -999
		= -999
		>> plain.has(2)
		= yes
		>> plain.has(456)
		= no

		>> fallback := {4=40; fallback=plain}
		>> fallback.has(1)
		= yes
		>> fallback[1] or -999
		= 10

	do:
		>> t4 := &{"one"= 1}
		>> t4["one"] = 999
		>> t4["two"] = 222
		>> t4
		= &{"one"=999, "two"=222}

	do:
		>> {1=1, 2=2} == {2=2, 1=1}
		= yes
		>> {1=1, 2=2} == {1=1, 2=999}
		= no

		>> {1=1, 2=2} <> {2=2, 1=1}
		= Int32(0)
		>> ints : [{Int=Int}] = [{}, {0=0}, {99=99}, {1=1, 2=2, 3=3}, {1=1, 99=99, 3=3}, {1=1, 2=-99, 3=3}, {1=1, 99=-99, 3=4}].sorted()
		= [{}, {0=0}, {1=1, 2=-99, 3=3}, {1=1, 2=2, 3=3}, {1=1, 99=99, 3=3}, {1=1, 99=-99, 3=4}, {99=99}]

		>> other_ints : [|Int|] = [||, |1|, |2|, |99|, |0, 3|, |1, 2|, |99|].sorted()
		= [||, |0, 3|, |1|, |1, 2|, |2|, |99|, |99|]

	do:
		# Default values:
		counter := &{"x"=10; default=0}
		>> counter["x"]
		= 10
		>> counter["y"]
		= 0
		>> counter.has("x")
		= yes
		>> counter.has("y")
		= no

		>> counter["y"] += 1
		>> counter
		>> counter
		= &{"x"=10, "y"=1; default=0}
