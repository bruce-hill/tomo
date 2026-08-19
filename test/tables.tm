test "table literal, lookup, and iteration"
	>> t := {"one": 1, "two": 2}
	>> t["one"]
	>> t["two"]
	>> t["???"]
	assert t == {"one": 1, "two": 2}
	assert t["one"] == 1
	assert t["two"] == 2
	assert t["???"] == none
	assert t["one"]! == 1
	assert t["???"] or -1 == -1

	>> t_str := ""
	for k,v in t.entries()
		t_str ++= "($k=$v)"
	>> t_str
	>> t.length
	>> t.keys
	>> t.values
	assert t_str == "(one=1)(two=2)"
	assert t.length == 2
	assert t.fallback == none
	assert t.keys == ["one", "two"]
	assert t.values == [1, 2]

test "fallback tables"
	>> t := {"one": 1, "two": 2}
	>> t2 := {"three": 3; fallback=t}
	>> t2["one"]
	>> t2["three"]
	>> t2.length
	>> t2.fallback
	assert t2 == {"three": 3; fallback={"one": 1, "two": 2}}
	assert t2["one"] == 1
	assert t2["three"] == 3
	assert t2["???"] == none
	assert t2.length == 1
	assert t2.fallback == {"one": 1, "two": 2}

	>> t2_str := ""
	for k,v in t2.entries()
		t2_str ++= "($k=$v)"
	>> t2_str
	assert t2_str == "(three=3)"

test "table comprehensions"
	assert {i: 10*i for i in 5} == {1: 10, 2: 20, 3: 30, 4: 40, 5: 50}
	assert {i: 10*i for i in 5 if i mod 2 != 0} == {1: 10, 3: 30, 5: 50}
	assert {x: 10*x for x in y if x > 1 for y in [3, 4, 5] if y < 5} == {2: 20, 3: 30, 4: 40}

test "heap-allocated table remove"
	>> t3 := @{1: 10, 2: 20, 3: 30}
	t3.remove(3)
	>> t3[]
	assert t3[] == {1: 10, 2: 20}

test "plain table and fallback lookup"
	>> plain := {1: 10, 2: 20, 3: 30}
	>> plain[2]!
	>> plain[456] or -999
	>> plain.has(2)
	>> plain.has(456)
	assert plain[2]! == 20
	assert plain[2]! == 20
	assert plain[456] or -999 == -999
	assert plain.has(2) == yes
	assert plain.has(456) == no

	>> fallback := {4: 40; fallback=plain}
	>> fallback.has(1)
	>> fallback[1] or -999
	assert fallback.has(1) == yes
	assert fallback[1] or -999 == 10

test "mutating a heap-allocated table"
	>> t4 := &{"one": 1}
	t4["one"] = 999
	t4["two"] = 222
	>> t4[]
	assert t4[] == {"one": 999, "two": 222}

test "table equality and comparison"
	>> {1: 1, 2: 2} == {2: 2, 1: 1}
	assert {1: 1, 2: 2} == {2: 2, 1: 1}
	assert {1: 1, 2: 2} != {1: 1, 2: 999}
	assert ({1: 1, 2: 2} <> {2: 2, 1: 1}) == Int32(0)
	>> ints : [{Int:Int}] = [{}, {0: 0}, {99: 99}, {1: 1, 2: 2, 3: 3}, {1: 1, 99: 99, 3: 3}, {1: 1, 2: -99, 3: 3}, {1: 1, 99: -99, 3: 4}]
	>> ints.sorted()
	assert ints.sorted() == [{}, {0: 0}, {1: 1, 2: -99, 3: 3}, {1: 1, 2: 2, 3: 3}, {1: 1, 99: 99, 3: 3}, {1: 1, 99: -99, 3: 4}, {99: 99}]

test "default values"
	# Default values:
	>> counter := &{"x": 10; default=0}
	>> counter["x"]
	>> counter["y"]
	>> counter.has("x")
	>> counter.has("y")
	assert counter["x"] == 10
	assert counter["y"] == 0
	assert counter.has("x") == yes
	assert counter.has("y") == no
	counter["y"] += 1
	>> counter["y"]
	assert counter["y"] == 1

	# A default in the type annotation lets an empty table index
	# non-optionally (needed because empty literals can't infer their types):
	empty : &{Text:Int; default=0} = &{}
	assert empty["missing"] == 0
	empty["a"] += 1
	empty["a"] += 1
	assert empty["a"] == 2
	# Terse `= default` type form is equivalent:
	terse : &{Text:Int = 0} = &{}
	terse["z"] += 5
	assert terse["z"] == 5

test "default on value but not type is rejected"
	# The default would be silently dropped (indexing would become optional),
	# so this is a compile error pointing at the type-annotation form:
	t : &{Int:Int} = &{; default=0}
	>> t[9]
fails_compile "default value, but the type"

test "set operations on tables"
	# Set operations
	>> a := {"A":1, "B":2, "C":3}
	>> b := {"B":2, "C":30, "D":40}
	>> a.with(b)
	>> a.intersection(b)
	>> a.difference(b)
	>> a.without(b)
	assert a.with(b) == {"A":1, "B":2, "C":30, "D":40}
	assert a.with(b) == a ++ b
	assert a.intersection(b) == {"B":2}
	assert a.difference(b) == {"A":1, "D":40}
	assert a.without(b) == {"A":1, "C":3}

test "set operations on sets"
	# Set operations with sets
	>> a := {"A", "B", "C"}
	>> b := {"B", "C", "D"}
	>> a.with(b)
	>> a.intersection(b)
	>> a.difference(b)
	>> a.without(b)
	assert a.with(b) == {"A", "B", "C", "D"}
	assert a.with(b) == a ++ b
	assert a.intersection(b) == {"B", "C"}
	assert a.difference(b) == {"A", "D"}
	assert a.without(b) == {"A"}

test "force-unwrapping a missing key panics"
	t := {1: 10, 2: 20}
	_ := t[999]!
fails "This key was not found in the table: 999"

test "table values must match the declared type"
	t : {Text:Int} = {"a": "b"}
fails_compile "I expected a Int here, but this is a Text"

test "skip and stop work in table loops"
	t := {"a": 1, "b": 2, "c": 3}
	n := 0
	for k, v in t.entries()
		if k == "a"
			skip
		n += v
		if n >= 5
			stop
	assert n == 5
