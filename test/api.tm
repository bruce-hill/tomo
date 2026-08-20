# This file contains auto-generated tests from the examples in api/*.yaml

test "Bool.parse"
    assert Bool.parse("yes") == yes
    assert Bool.parse("no") == no
    assert Bool.parse("???") == none
    
    assert Bool.parse("yesJUNK") == none
    remainder : Text
    assert Bool.parse("yesJUNK", &remainder) == yes
    assert remainder == "JUNK"

test "Byte.get_bit"
    assert not Byte(6).get_bit(1)
    assert Byte(6).get_bit(2)
    assert Byte(6).get_bit(3)
    assert not Byte(6).get_bit(4)

test "Byte.hex"
    assert Byte(18).hex(prefix=yes) == "0x12"

test "Byte.is_between"
    assert Byte(7).is_between(1, 10)
    assert Byte(7).is_between(10, 1)
    assert not Byte(7).is_between(100, 200)
    assert Byte(7).is_between(1, 7)

test "Byte.parse"
    assert Byte.parse("5") == Byte(5)
    assert Byte.parse("asdf") == none
    assert Byte.parse("123xyz") == none
    
    remainder : Text
    assert Byte.parse("123xyz", remainder=&remainder) == Byte(123)
    assert remainder == "xyz"

test "Byte.to"
    iter := Byte(2).to(4)
    assert iter() == Byte(2)
    assert iter() == Byte(3)
    assert iter() == Byte(4)
    assert iter() == none
    
    assert [x for x in Byte(2).to(5)] == [Byte(2), Byte(3), Byte(4), Byte(5)]
    assert [x for x in Byte(5).to(2)] == [Byte(5), Byte(4), Byte(3), Byte(2)]
    assert [x for x in Byte(2).to(5, step=2)] == [Byte(2), Byte(4)]

test "CString.as_text"
    assert CString("Hello").as_text() == "Hello"

test "CString.join"
    assert CString(",").join([CString("a"), CString("b")]) == CString("a,b")

test "Int.abs"
    assert (-10).abs() == 10

test "Int.choose"
    assert 4.choose(2) == 6

test "Int.clamped"
    assert 2.clamped(5, 10) == 5

test "Int.factorial"
    assert 10.factorial() == 3628800

test "Int.get_bit"
    assert not 6.get_bit(1)
    assert 6.get_bit(2)
    assert 6.get_bit(3)
    assert not 6.get_bit(4)

test "Int.hex"
    assert 255.hex(digits=4, uppercase=yes, prefix=yes) == "0x00FF"

test "Int.is_between"
    assert 7.is_between(1, 10)
    assert 7.is_between(10, 1)
    assert not 7.is_between(100, 200)
    assert 7.is_between(1, 7)

test "Int.is_prime"
    assert 7.is_prime()
    assert not 6.is_prime()

test "Int.next_prime"
    assert 11.next_prime() == 13

test "Int.octal"
    assert 64.octal(digits=4, prefix=yes) == "0o0100"

test "Int.onward"
    nums : &[Int] = &[]
    for i in 5.onward()
        nums.insert(i)
        break if i == 10
    assert nums[] == [5, 6, 7, 8, 9, 10]

test "Int.parse"
    assert Int.parse("123") == 123
    assert Int.parse("0xFF") == 255
    assert Int.parse("123xyz") == none
    remainder : Text
    assert Int.parse("123xyz", remainder=&remainder) == 123
    assert remainder == "xyz"
    
    # Can't parse:
    assert Int.parse("asdf") == none
    
    # Outside valid range:
    assert Int8.parse("9999999") == none
    
    # Explicitly specifying base:
    assert Int.parse("10", base=16) == 16

test "Int.sqrt"
    assert 16.sqrt() == 4
    assert 17.sqrt() == 4

test "Int.to"
    iter := 2.to(5)
    assert iter() == 2
    assert iter() == 3
    assert iter() == 4
    assert iter() == 5
    assert iter() == none
    
    assert [x for x in 2.to(5)] == [2, 3, 4, 5]
    assert [x for x in 5.to(2)] == [5, 4, 3, 2]
    assert [x for x in 2.to(5, step=2)] == [2, 4]

test "List.binary_search"
    # Find an item:
    assert [10, 20, 30, 40].binary_search(func(x:Int) x == 30) == 3
    # No such item:
    assert [10, 20, 30, 40].binary_search(func(x:Int) x == 25) == none
    # Find an insertion point (where an item would go to preserve sort order):
    assert [10, 20, 30, 40].binary_search(func(x:Int) x >= 25) == 3

test "List.by"
    assert [1, 2, 3, 4, 5, 6].by(2) == [1, 3, 5]

test "List.clear"
    list := &[10, 20]
    list.clear()
    assert list[] == []

test "List.counts"
    assert [10, 20, 30, 30, 30].counts() == {10:1, 20:1, 30:3}

test "List.find"
    assert [10, 20, 30, 40, 50].find(20) == 2
    assert [10, 20, 30, 40, 50].find(9999) == none

test "List.from"
    assert [10, 20, 30, 40, 50].from(3) == [30, 40, 50]

test "List.has"
    assert [10, 20, 30].has(20)

test "List.heap_pop"
    my_heap := &[30, 10, 20]
    my_heap.heapify()
    assert my_heap.heap_pop() == 10

test "List.heap_push"
    my_heap : &[Int]
    my_heap.heap_push(10)
    assert my_heap.heap_pop() == 10

test "List.heapify"
    my_heap := &[30, 10, 20]
    my_heap.heapify()

test "List.insert"
    list := &[10, 20]
    list.insert(30)
    assert list == [10, 20, 30]
    
    list.insert(999, at=2)
    assert list == [10, 999, 20, 30]

test "List.insert_all"
    list := &[10, 20]
    list.insert_all([30, 40])
    assert list == [10, 20, 30, 40]
    
    list.insert_all([99, 100], at=2)
    assert list == [10, 99, 100, 20, 30, 40]

test "List.pairs"
    assert ["$a$b" for a, b in [1, 2, 3].pairs()] == ["12", "13", "23"]

test "List.pop"
    list := &[10, 20, 30, 40]
    
    assert list.pop() == 40
    assert list[] == [10, 20, 30]
    
    assert list.pop(index=2) == 20
    assert list[] == [10, 30]

test "List.random"
    nums := [10, 20, 30]
    pick := nums.random()!
    assert nums.has(pick)
    empty : [Int]
    assert empty.random() == none

test "List.remove_at"
    list := &[10, 20, 30, 40, 50]
    list.remove_at(2)
    assert list == [10, 30, 40, 50]
    
    list.remove_at(2, count=2)
    assert list == [10, 50]

test "List.remove_item"
    list := &[10, 20, 10, 20, 30]
    list.remove_item(10)
    assert list == [20, 20, 30]
    
    list.remove_item(20, max_count=1)
    assert list == [20, 30]

test "List.reversed"
    assert [10, 20, 30].reversed() == [30, 20, 10]

test "List.sample"
    _ := [10, 20, 30].sample(2, weights=[90%, 5%, 5%]) # E.g. [10, 10]

test "List.shuffle"
    nums := &[10, 20, 30, 40]
    nums.shuffle()
    # E.g. [20, 40, 10, 30]

test "List.shuffled"
    nums := [10, 20, 30, 40]
    _ := nums.shuffled()
    # E.g. [20, 40, 10, 30]

test "List.slice"
    assert [10, 20, 30, 40, 50].slice(2, 4) == [20, 30, 40]
    assert [10, 20, 30, 40, 50].slice(-3, -2) == [30, 40]

test "List.sort"
    list := &[40, 10, -30, 20]
    list.sort()
    assert list == [-30, 10, 20, 40]
    
    list.sort(func(a,b:Int) a.abs() <> b.abs())
    assert list == [10, 20, -30, 40]

test "List.sorted"
    assert [40, 10, -30, 20].sorted() == [-30, 10, 20, 40]
    assert [40, 10, -30, 20].sorted(
       func(a,b:Int) a.abs() <> b.abs()
    ) == [10, 20, -30, 40]

test "List.swap"
    list := &[10, 20, 30]
    list.swap(1, 3)
    assert list == [30, 20, 10]
    
    list.swap(2, -1)
    assert list == [30, 10, 20]

test "List.to"
    assert [10, 20, 30, 40, 50].to(3) == [10, 20, 30]
    assert [10, 20, 30, 40, 50].to(-2) == [10, 20, 30, 40]

test "List.unique"
    assert [10, 20, 10, 10, 30].unique() == {10, 20, 30}

test "List.where"
    assert ["BC", "ABC", "CD"].where(func(t:Text) t.starts_with("A")) == 2
    assert ["BC", "ABC", "CD"].where(func(t:Text) t.starts_with("X")) == none

test "Num.abs"
    assert (-3.5).abs() == 3.5

test "Num.acos"
    assert (0.0).acos().near(1.5707963267948966)

test "Num.acosh"
    assert (1.0).acosh() == 0

test "Num.asin"
    assert (0.5).asin().near(0.5235987755982989)

test "Num.asinh"
    assert (0.0).asinh() == 0

test "Num.atan"
    assert (1.0).atan().near(0.7853981633974483)

test "Num.atan2"
    assert Num.atan2(1, 1).near(0.7853981633974483)

test "Num.atanh"
    assert (0.5).atanh().near(0.5493061443340549)

test "Num.cbrt"
    assert (27.0).cbrt() == 3

test "Num.ceil"
    assert (3.2).ceil() == 4

test "Num.clamped"
    assert (2.5).clamped(5.5, 10.5) == 5.5

test "Num.copysign"
    assert (3.0).copysign(-1) == -3

test "Num.cos"
    assert (0.0).cos() == 1

test "Num.cosh"
    assert (0.0).cosh() == 1

test "Num.erf"
    assert (0.0).erf() == 0

test "Num.erfc"
    assert (0.0).erfc() == 1

test "Num.exp"
    assert (1.0).exp().near(2.718281828459045)

test "Num.exp2"
    assert (3.0).exp2() == 8

test "Num.expm1"
    assert (1.0).expm1().near(1.7182818284590453)

test "Num.fdim"
    assert (5.0).fdim(3) == 2

test "Num.floor"
    assert (3.7).floor() == 3

test "Num.hypot"
    assert Num.hypot(3, 4) == 5

test "Num.is_between"
    assert (7.5).is_between(1, 10)
    assert (7.5).is_between(10, 1)
    assert not (7.5).is_between(100, 200)
    assert (7.5).is_between(1, 7.5)

test "Num.isfinite"
    assert (1.0).isfinite()
    assert not Num.INF.isfinite()

test "Num.isinf"
    assert Num.INF.isinf()
    assert not (1.0).isinf()

test "Num.j0"
    assert (0.0).j0() == 1

test "Num.j1"
    assert (0.0).j1() == 0

test "Num.log"
    assert Num.E.log() == 1

test "Num.log10"
    assert (100.0).log10() == 2

test "Num.log1p"
    assert (1.0).log1p().near(0.6931471805599453)

test "Num.log2"
    assert (8.0).log2() == 3

test "Num.logb"
    assert (8.0).logb() == 3

test "Num.mix"
    assert (0.5).mix(10, 20) == 15
    assert (0.25).mix(10, 20) == 12.5

test "Num.near"
    assert (1.0).near(1.000000001)
    assert (100.0).near(110, ratio=0.1)
    assert (5.0).near(5.1, min_epsilon=0.1)

test "Num.nextafter"
    assert (1.0).nextafter(1.1) == 1.0000000000000002

test "Num.parse"
    assert Num.parse("3.14") == 3.14
    assert Num.parse("1e3") == 1000
    assert Num.parse("1.5junk") == none
    remainder : Text
    assert Num.parse("1.5junk", &remainder) == 1.5
    assert remainder == "junk"

test "Num.percent"
    assert (0.5).percent() == "50%"
    assert (1./3.).percent(2) == "34%"
    assert (1./3.).percent(precision=0.0001) == "33.3333%"
    assert (1./3.).percent(precision=10.) == "30%"

test "Num.rint"
    assert (3.5).rint() == 4
    assert (2.5).rint() == 2

test "Num.round"
    assert (2.3).round() == 2
    assert (2.7).round() == 3

test "Num.significand"
    assert (1234.567).significand() == 1.2056318359375

test "Num.sin"
    assert (0.0).sin() == 0

test "Num.sinh"
    assert (0.0).sinh() == 0

test "Num.sqrt"
    assert (16.0).sqrt() == 4

test "Num.tan"
    assert (0.0).tan() == 0

test "Num.tanh"
    assert (0.0).tanh() == 0

test "Num.tgamma"
    assert (1.0).tgamma() == 1

test "Num.trunc"
    assert (3.7).trunc() == 3
    assert (-3.7).trunc() == -3

test "Num.with_precision"
    assert (0.1234567).with_precision(0.01) == 0.12
    assert (123456.).with_precision(100) == 123500
    assert (1234567.).with_precision(5) == 1234565

test "Num.y0"
    assert (1.0).y0().near(0.08825696421567698)

test "Num.y1"
    assert (1.0).y1().near(-0.7812128213002887)

test "Path.accessed"
    if no
        assert (./file.txt).accessed() == Int64(1704221100)
        assert (./not-a-file).accessed() == none

test "Path.append"
    if no
        (./log.txt).append("extra line\n")!

test "Path.append_bytes"
    if no
        (./log.txt).append_bytes([104, 105])!

test "Path.base_name"
    if no
        assert (./path/to/file.txt).base_name() == "file.txt"

test "Path.by_line"
    if no
        # Safely handle file not being readable:
        if lines := (./file.txt).by_line()
            for line in lines
                say(line.upper())
        else
            say("Couldn't read file!")
        
        # Assume the file is readable and error if that's not the case:
        for line in (/dev/stdin).by_line()!
            say(line.upper())

test "Path.byte_writer"
    if no
        write := (./file.txt).byte_writer()
        write("Hello\n".utf8())!
        write("world\n".utf8(), close=yes)!

test "Path.can_execute"
    if no
        assert (/bin/sh).can_execute()
        assert not (/usr/include/stdlib.h).can_execute()
        assert not (/non/existant/file).can_execute()

test "Path.can_read"
    if no
        assert (/usr/include/stdlib.h).can_read()
        assert not (/etc/shadow).can_read()
        assert not (/non/existant/file).can_read()

test "Path.can_write"
    if no
        assert (/tmp).can_write()
        assert not (/etc/passwd).can_write()
        assert not (/non/existant/file).can_write()

test "Path.changed"
    if no
        assert (./file.txt).changed() == Int64(1704221100)
        assert (./not-a-file).changed() == none

test "Path.child"
    if no
        assert (./directory).child("file.txt") == (./directory/file.txt)

test "Path.children"
    if no
        assert (./directory).children(include_hidden=yes) == [(./directory/.git), (./directory/foo.txt)]

test "Path.components"
    if no
        assert (./foo/baz.txt).components() == [".", "foo", "baz.txt"]
        assert (/absolute/path/).components() == ["/", "absolute", "path"]

test "Path.copy_to"
    if no
        (./file.txt).move(/tmp/renamed.txt)!

test "Path.create_directory"
    if no
        (./new_directory).create_directory()!

test "Path.current_dir"
    if no
        assert Path.current_dir() == (/home/user/tomo)

test "Path.each_child"
    if no
        for child in (/dir).each_child()
            say("Child: $child")

test "Path.exists"
    if no
        assert (/).exists()

test "Path.expand_home"
    if no
        # Assume current user is 'user'
        assert (~/foo).expand_home() == (/home/user/foo)
        # No change
        assert (/foo).expand_home() == (/foo)

test "Path.extension"
    if no
        assert (./file.tar.gz).extension() == "tar.gz"
        assert (./file.tar.gz).extension(full=no) == "gz"
        assert (/foo).extension() == ""
        assert (./.git).extension() == ""

test "Path.files"
    if no
        assert (./directory).files(include_hidden=yes) == [(./directory/file1.txt), (./directory/file2.txt)]

test "Path.glob"
    if no
        # Current directory includes: foo.txt, baz.txt, qux.jpg, .hidden
        assert (./*).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
        assert (./*.txt).glob() == [(./foo.txt), (./baz.txt)]
        assert (./*.{txt,jpg}).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
        assert (./.*).glob() == [(./.hidden)]
        
        # Globs with no matches return an empty list:
        assert (./*.xxx).glob() == []

test "Path.group"
    if no
        assert (/bin).group() == "root"
        assert (/non/existent/file).group() == none

test "Path.has_extension"
    if no
        assert (/foo.txt).has_extension("txt")
        assert (/foo.txt).has_extension(".txt")
        assert (/foo.tar.gz).has_extension("gz")
        assert not (/foo.tar.gz).has_extension("zip")

test "Path.is_directory"
    if no
        assert (./directory/).is_directory()
        assert not (./file.txt).is_directory()

test "Path.is_file"
    if no
        assert (./file.txt).is_file()
        assert not (./directory/).is_file()

test "Path.is_socket"
    if no
        assert (./socket).is_socket()

test "Path.is_symlink"
    if no
        assert (./link).is_symlink()

test "Path.lines"
    if no
        lines := (./file.txt).lines()!

test "Path.matches_glob"
    if no
        assert (./file.txt).matches_glob("*.txt")
        assert (./file.c).matches_glob("*.{c,h}")

test "Path.modified"
    if no
        assert (./file.txt).modified() == Int64(1704221100)
        assert (./not-a-file).modified() == none

test "Path.move"
    if no
        (./file.txt).move(/tmp/renamed.txt)!

test "Path.owner"
    if no
        assert (/bin).owner() == "root"
        assert (/non/existent/file).owner() == none

test "Path.parent"
    if no
        assert (./path/to/file.txt).parent() == (./path/to/)

test "Path.read"
    if no
        assert (./hello.txt).read() == "Hello"
        assert (./nosuchfile.xxx).read() == none

test "Path.read_bytes"
    if no
        assert (./hello.txt).read_bytes()! == [72, 101, 108, 108, 111]
        assert (./nosuchfile.xxx).read_bytes() == none

test "Path.relative_to"
    if no
        assert (./path/to/file.txt).relative_to((./path)) == (./to/file.txt)
        assert (/tmp/foo).relative_to((/tmp)) == (./foo)

test "Path.remove"
    if no
        (./file.txt).remove()!

test "Path.resolved"
    if no
        assert (~/foo).resolved() == (/home/user/foo)
        assert (./path/to/file.txt).resolved(relative_to=(/foo)) == (/foo/path/to/file.txt)

test "Path.set_owner"
    if no
        (./file.txt).set_owner(owner="root", group="wheel")!

test "Path.sibling"
    if no
        assert (/foo/baz).sibling("doop") == (/foo/doop)

test "Path.subdirectories"
    if no
        assert (./directory).subdirectories() == [(./directory/subdir1), (./directory/subdir2)]
        assert (./directory).subdirectories(include_hidden=yes) == [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]

test "Path.unique_directory"
    if no
        created := (/tmp/my-dir.XXXXXX).unique_directory()
        assert created.is_directory()
        created.remove()!

test "Path.walk"
    if no
        for p in (/tmp).walk()
            say("File or dir: $p")
        
        # The path itself is always included:
        assert [p for p in (./file.txt).walk()] == [(./file.txt)]

test "Path.write"
    if no
        (./file.txt).write("Hello, world!")!

test "Path.write_bytes"
    if no
        (./file.txt).write_bytes([104, 105])!

test "Path.write_unique"
    if no
        created := (./file-XXXXXX.txt).write_unique("Hello, world!")!
        assert created == (./file-27QHtq.txt)
        assert created.read()! == "Hello, world!"
        created.remove()!

test "Path.write_unique_bytes"
    if no
        created := (./file-XXXXXX.txt).write_unique_bytes([1, 2, 3])!
        assert created == (./file-27QHtq.txt)
        assert created.read_bytes()! == [1, 2, 3]
        created.remove()!

test "Path.writer"
    if no
        write := (./file.txt).writer()
        write("Hello\n")!
        write("world\n", close=yes)!

test "Table.clear"
    t := &{"A":1}
    t.clear()
    assert t[] == {}

test "Table.difference"
    t1 := {"A": 1, "B": 2, "C": 3}
    t2 := {"B": 2, "C":30, "D": 40}
    assert t1.difference(t2) == {"A": 1, "D": 40}

test "Table.entries"
    t := {"A": 1, "B": 2}
    assert ["$k=$v" for k, v in t.entries()] == ["A=1", "B=2"]

test "Table.get"
    t := {"A": 1, "B": 2}
    assert t.get("A") == 1
    assert t.get("????") == none
    assert t.get("A")! == 1
    assert t.get("????") or 0 == 0

test "Table.get_or_set"
    t := &{"A": @[1, 2, 3]; default=@[]}
    t.get_or_set("A").insert(4)
    t.get_or_set("B").insert(99)
    assert t["A"][] == [1, 2, 3, 4]
    assert t["B"][] == [99]
    assert t.get_or_set("C", @[0, 0, 0])[] == [0, 0, 0]

test "Table.has"
    assert {"A": 1, "B": 2}.has("A")
    assert not {"A": 1, "B": 2}.has("xxx")

test "Table.intersection"
    t1 := {"A": 1, "B": 2, "C": 3}
    t2 := {"B": 2, "C":30, "D": 40}
    assert t1.intersection(t2) == {"B": 2}

test "Table.remove"
    t := &{"A": 1, "B": 2}
    t.remove("A")
    assert t == {"B": 2}

test "Table.set"
    t := &{"A": 1, "B": 2}
    t.set("C", 3)
    assert t == {"A": 1, "B": 2, "C": 3}

test "Table.with"
    t := {"A": 1, "B": 2}
    assert t.with({"B": 20, "C": 30}) == {"A": 1, "B": 20, "C": 30}

test "Table.with_fallback"
    t := {"A": 1; fallback={"B": 2}}
    t2 := t.with_fallback({"B": 3})
    assert t2["B"] == 3
    t3 := t.with_fallback(none)
    assert t3["B"] == none

test "Table.without"
    t := {"A": 1, "B": 2, "C": 3}
    assert t.without({"B": 2, "C": 30, "D": 40}) == {"A": 1, "C": 3}

test "Text.as_c_string"
    assert "Hello".as_c_string() == CString("Hello")

test "Text.at"
    assert "Amélie".at(3) == "é"

test "Text.by_line"
    text := "
        line one
        line two
    "
    lines := [line for line in text.by_line()]
    assert lines == ["line one", "line two"]

test "Text.by_split"
    text := "one,two,three"
    chunks := [chunk for chunk in text.by_split(",")]
    assert chunks == ["one", "two", "three"]

test "Text.by_split_any"
    text := "one,two,;,three"
    chunks := [chunk for chunk in text.by_split_any(",;")]
    assert chunks == ["one", "two", "three"]

test "Text.caseless_equals"
    assert "A".caseless_equals("a")
    
    # Turkish lowercase "I" is "ı" (dotless I), not "i"
    assert not "I".caseless_equals("i", language="tr_TR")

test "Text.codepoint_names"
    assert "Amélie".codepoint_names() == [
        "LATIN CAPITAL LETTER A",
        "LATIN SMALL LETTER M",
        "LATIN SMALL LETTER E WITH ACUTE",
        "LATIN SMALL LETTER L",
        "LATIN SMALL LETTER I",
        "LATIN SMALL LETTER E",
    ]

test "Text.distance"
    assert "hello".distance("hello") == 0
    texts := &["goodbye", "hello", "hallo"]
    texts.sort(func(a,b:Text) a.distance("hello") <> b.distance("hello"))
    assert texts == ["hello", "hallo", "goodbye"]

test "Text.ends_with"
    assert "hello world".ends_with("world")
    remainder : Text
    assert "hello world".ends_with("world", &remainder)
    assert remainder == "hello "

test "Text.find"
    assert "one two".find("one") == 1
    assert "one two".find("two") == 5
    assert "one two".find("three") == none
    assert "one two".find("o", start=2) == 7

test "Text.from"
    assert "hello".from(2) == "ello"
    assert "hello".from(-2) == "lo"

test "Text.from_c_string"
    assert Text.from_c_string(CString("Hello")) == "Hello"

test "Text.from_codepoint_names"
    text := Text.from_codepoint_names([
        "LATIN CAPITAL LETTER A WITH RING ABOVE",
        "LATIN SMALL LETTER K",
        "LATIN SMALL LETTER E",
    ])
    assert text == "Åke"

test "Text.from_utf16"
    assert Text.from_utf16([197, 107, 101]) == "Åke"
    assert Text.from_utf16([12371, 12435, 12395, 12385, 12399, 19990, 30028]) == "こんにちは世界"

test "Text.from_utf32"
    assert Text.from_utf32([197, 107, 101]) == "Åke"

test "Text.from_utf8"
    assert Text.from_utf8([195, 133, 107, 101]) == "Åke"

test "Text.has"
    assert "hello world".has("wo")
    assert not "hello world".has("xxx")

test "Text.join"
    assert ", ".join(["one", "two", "three"]) == "one, two, three"

test "Text.left_pad"
    assert "x".left_pad(5) == "    x"
    assert "x".left_pad(5, "ABC") == "ABCAx"

test "Text.lines"
    assert "one\ntwo\nthree".lines() == ["one", "two", "three"]
    assert "one\ntwo\nthree\n".lines() == ["one", "two", "three"]
    assert "one\ntwo\nthree\n\n".lines() == ["one", "two", "three", ""]
    assert "one\r\ntwo\r\nthree\r\n".lines() == ["one", "two", "three"]
    assert "".lines() == []

test "Text.lower"
    assert "AMÉLIE".lower() == "amélie"
    assert "I".lower(language="tr_TR") == "ı"

test "Text.matches_glob"
    assert "hello world".matches_glob("h* *d")

test "Text.middle_pad"
    assert "x".middle_pad(6) == "  x   "
    assert "x".middle_pad(10, "ABC") == "ABCAxABCAB"

test "Text.quoted"
    assert "one\ntwo".quoted() == "\"one\\ntwo\""

test "Text.repeat"
    assert "Abc".repeat(3) == "AbcAbcAbc"

test "Text.replace"
    assert "Hello world".replace("world", "there") == "Hello there"

test "Text.reversed"
    assert "Abc".reversed() == "cbA"

test "Text.right_pad"
    assert "x".right_pad(5) == "x    "
    assert "x".right_pad(5, "ABC") == "xABCA"

test "Text.slice"
    assert "hello".slice(2, 3) == "el"
    assert "hello".slice(to=-2) == "hell"
    assert "hello".slice(from=2) == "ello"

test "Text.split"
    assert "one,two,,three".split(",") == ["one", "two", "", "three"]
    assert "abc".split() == ["a", "b", "c"]

test "Text.split_any"
    assert "one, two,,three".split_any(", ") == ["one", "two", "three"]

test "Text.starts_with"
    assert "hello world".starts_with("hello")
    remainder : Text
    assert "hello world".starts_with("hello", &remainder)
    assert remainder == " world"

test "Text.title"
    assert "amélie".title() == "Amélie"
    
    # In Turkish, uppercase "i" is "İ"
    assert "i".title(language="tr_TR") == "İ"

test "Text.to"
    assert "goodbye".to(3) == "goo"
    assert "goodbye".to(-2) == "goodby"

test "Text.translate"
    text := "A <tag> & an ampersand".translate({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot",
        "'": "&#39;",
    })
    assert text == "A &lt;tag&gt; &amp; an ampersand"

test "Text.trim"
    assert "   x y z    \n".trim() == "x y z"
    assert "one,".trim(",") == "one"
    assert "   xyz   ".trim(right=no) == "xyz   "

test "Text.upper"
    assert "amélie".upper() == "AMÉLIE"
    
    # In Turkish, uppercase "i" is "İ"
    assert "i".upper(language="tr_TR") == "İ"

test "Text.utf16"
    assert "Åke".utf16() == [197, 107, 101]
    assert "こんにちは世界".utf16() == [12371, 12435, 12395, 12385, 12399, 19990, 30028]

test "Text.utf32"
    assert "Amélie".utf32() == [65, 109, 233, 108, 105, 101]

test "Text.utf8"
    assert "Amélie".utf8() == [65, 109, 195, 169, 108, 105, 101]

test "Text.width"
    assert "Amélie".width() == 6
    assert "🤠".width() == 2

test "Text.without_prefix"
    assert "foo:baz".without_prefix("foo:") == "baz"
    assert "qux".without_prefix("foo:") == "qux"

test "Text.without_suffix"
    assert "baz.foo".without_suffix(".foo") == "baz"
    assert "qux".without_suffix(".foo") == "qux"

test "ask"
    if no
        assert ask("What's your name? ") == "Arthur Dent"

test "at_cleanup"
    if no
        at_cleanup(func()
            _ := (/tmp/file.txt).remove(ignore_missing=yes)
        )

test "exit"
    if no
        exit("Goodbye forever!", Int32(1))

test "fail"
    if no
        fail("Oh no!")

test "getenv"
    if no
        assert getenv("TERM") == "xterm-256color"
        assert getenv("not_a_variable") == none

test "print"
    if no
        print("Hello ", newline=no)
        print("world!")

test "say"
    if no
        say("Hello ", newline=no)
        say("world!")

test "setenv"
    if no
        setenv("FOOBAR", "xyz")

test "sleep"
    if no
        sleep(1.5)
