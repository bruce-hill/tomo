func main()
	t := {"one": 1, "two": 2}
	assert t == {"one": 1, "two": 2}

	assert t["one"] == 1
	assert t["two"] == 2
	assert t["???"] == none
	assert t["one"]! == 1
	assert t["???"] or -1 == -1

	t_str := ""
	for k,v in t
		t_str ++= "($k=$v)"
	assert t_str == "(one=1)(two=2)"

	assert t.length == 2
	assert t.fallback == none

	assert t.keys == ["one", "two"]
	assert t.values == [1, 2]

	t2 := {"three": 3; fallback=t}
	assert t2 == {"three": 3; fallback={"one": 1, "two": 2}}

	assert t2["one"] == 1
	assert t2["three"] == 3
	assert t2["???"] == none

	assert t2.length == 1
	assert t2.fallback == {"one": 1, "two": 2}

	t2_str := ""
	for k,v in t2
		t2_str ++= "($k=$v)"
	assert t2_str == "(three=3)"

	assert {i: 10*i for i in 5} == {1: 10, 2: 20, 3: 30, 4: 40, 5: 50}
	assert {i: 10*i for i in 5 if i mod 2 != 0} == {1: 10, 3: 30, 5: 50}
	assert {x: 10*x for x in y if x > 1 for y in [3, 4, 5] if y < 5} == {2: 20, 3: 30, 4: 40}

	>> t3 := @{1: 10, 2: 20, 3: 30}
	>> t3.remove(3)
	assert t3[] == {1: 10, 2: 20}

	do
		>> plain := {1: 10, 2: 20, 3: 30}
		assert plain[2]! == 20
		assert plain[2]! == 20
		assert plain[456] or -999 == -999
		assert plain.has(2) == yes
		assert plain.has(456) == no

		>> fallback := {4: 40; fallback=plain}
		assert fallback.has(1) == yes
		assert fallback[1] or -999 == 10

	do
		>> t4 := &{"one": 1}
		>> t4["one"] = 999
		>> t4["two"] = 222
		assert t4[] == {"one": 999, "two": 222}

	do
		assert {1: 1, 2: 2} == {2: 2, 1: 1}
		assert {1: 1, 2: 2} != {1: 1, 2: 999}

		assert ({1: 1, 2: 2} <> {2: 2, 1: 1}) == Int32(0)
		ints : [{Int:Int}] = [{}, {0: 0}, {99: 99}, {1: 1, 2: 2, 3: 3}, {1: 1, 99: 99, 3: 3}, {1: 1, 2: -99, 3: 3}, {1: 1, 99: -99, 3: 4}]
		assert ints.sorted() == [{}, {0: 0}, {1: 1, 2: -99, 3: 3}, {1: 1, 2: 2, 3: 3}, {1: 1, 99: 99, 3: 3}, {1: 1, 99: -99, 3: 4}, {99: 99}]

	do
		# Default values:
		counter := &{"x": 10; default=0}
		assert counter["x"] == 10
		assert counter["y"] == 0
		assert counter.has("x") == yes
		assert counter.has("y") == no

		>> counter["y"] += 1
