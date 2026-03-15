<<<<<<< HEAD
# This file contains auto-generated tests from the examples in api/*.yaml

func main()
    do # Test Bool.parse
        assert Bool.parse("yes") == yes
        assert Bool.parse("no") == no
        assert Bool.parse("???") == none
        
        assert Bool.parse("yesJUNK") == none
        remainder : Text
        assert Bool.parse("yesJUNK", &remainder) == yes
        assert remainder == "JUNK"

    do # Test Byte.get_bit
        assert Byte(6).get_bit(1) == no
        assert Byte(6).get_bit(2) == yes
        assert Byte(6).get_bit(3) == yes
        assert Byte(6).get_bit(4) == no

    do # Test Byte.hex
        assert Byte(18).hex(prefix=yes) == "0x12"

    do # Test Byte.is_between
        assert Byte(7).is_between(1, 10) == yes
        assert Byte(7).is_between(10, 1) == yes
        assert Byte(7).is_between(100, 200) == no
        assert Byte(7).is_between(1, 7) == yes

    do # Test Byte.parse
        assert Byte.parse("5") == Byte(5)
        assert Byte.parse("asdf") == none
        assert Byte.parse("123xyz") == none
        
        remainder : Text
        assert Byte.parse("123xyz", remainder=&remainder) == Byte(123)
        assert remainder == "xyz"

    do # Test Byte.to
        iter := Byte(2).to(4)
        assert iter() == Byte(2)
        assert iter() == Byte(3)
        assert iter() == Byte(4)
        assert iter() == none
        
        assert [x for x in Byte(2).to(5)] == [Byte(2), Byte(3), Byte(4), Byte(5)]
        assert [x for x in Byte(5).to(2)] == [Byte(5), Byte(4), Byte(3), Byte(2)]
        assert [x for x in Byte(2).to(5, step=2)] == [Byte(2), Byte(4)]

    do # Test CString.as_text
        assert CString("Hello").as_text() == "Hello"

    do # Test CString.join
        assert CString(",").join([CString("a"), CString("b")]) == CString("a,b")

    do # Test Int.abs
        assert (-10).abs() == 10

    do # Test Int.choose
        assert 4.choose(2) == 6

    do # Test Int.clamped
        assert 2.clamped(5, 10) == 5

    do # Test Int.factorial
        assert 10.factorial() == 3628800

    do # Test Int.get_bit
        assert 6.get_bit(1) == no
        assert 6.get_bit(2) == yes
        assert 6.get_bit(3) == yes
        assert 6.get_bit(4) == no

    do # Test Int.hex
        assert 255.hex(digits=4, uppercase=yes, prefix=yes) == "0x00FF"

    do # Test Int.is_between
        assert 7.is_between(1, 10) == yes
        assert 7.is_between(10, 1) == yes
        assert 7.is_between(100, 200) == no
        assert 7.is_between(1, 7) == yes

    do # Test Int.is_prime
        assert 7.is_prime() == yes
        assert 6.is_prime() == no

    do # Test Int.next_prime
        assert 11.next_prime() == 13

    do # Test Int.octal
        assert 64.octal(digits=4, prefix=yes) == "0o0100"

    do # Test Int.onward
        nums : &[Int] = &[]
        for i in 5.onward()
            nums.insert(i)
            stop if i == 10
        assert nums[] == [5, 6, 7, 8, 9, 10]

    do # Test Int.parse
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

    do # Test Int.sqrt
        assert 16.sqrt() == 4
        assert 17.sqrt() == 4

    do # Test Int.to
        iter := 2.to(5)
        assert iter() == 2
        assert iter() == 3
        assert iter() == 4
        assert iter() == 5
        assert iter() == none
        
        assert [x for x in 2.to(5)] == [2, 3, 4, 5]
        assert [x for x in 5.to(2)] == [5, 4, 3, 2]
        assert [x for x in 2.to(5, step=2)] == [2, 4]

    do # Test List.binary_search
        assert [1, 3, 5, 7, 9].binary_search(5) == 3
        assert [1, 3, 5, 7, 9].binary_search(-999) == 1
        assert [1, 3, 5, 7, 9].binary_search(999) == 6

    do # Test List.by
        assert [1, 2, 3, 4, 5, 6].by(2) == [1, 3, 5]

    do # Test List.clear
        list := &[10, 20]
        list.clear()
        assert list[] == []

    do # Test List.counts
        assert [10, 20, 30, 30, 30].counts() == {10:1, 20:1, 30:3}

    do # Test List.find
        assert [10, 20, 30, 40, 50].find(20) == 2
        assert [10, 20, 30, 40, 50].find(9999) == none

    do # Test List.from
        assert [10, 20, 30, 40, 50].from(3) == [30, 40, 50]

    do # Test List.has
        assert [10, 20, 30].has(20) == yes

    do # Test List.heap_pop
        my_heap := &[30, 10, 20]
        my_heap.heapify()
        assert my_heap.heap_pop() == 10

    do # Test List.heap_push
        my_heap : &[Int]
        my_heap.heap_push(10)
        assert my_heap.heap_pop() == 10

    do # Test List.heapify
        my_heap := &[30, 10, 20]
        my_heap.heapify()

    do # Test List.insert
        list := &[10, 20]
        list.insert(30)
        assert list == [10, 20, 30]
        
        list.insert(999, at=2)
        assert list == [10, 999, 20, 30]

    do # Test List.insert_all
        list := &[10, 20]
        list.insert_all([30, 40])
        assert list == [10, 20, 30, 40]
        
        list.insert_all([99, 100], at=2)
        assert list == [10, 99, 100, 20, 30, 40]

    do # Test List.pop
        list := &[10, 20, 30, 40]
        
        assert list.pop() == 40
        assert list[] == [10, 20, 30]
        
        assert list.pop(index=2) == 20
        assert list[] == [10, 30]

    do # Test List.random
        nums := [10, 20, 30]
        pick := nums.random()
        assert nums.has(pick)
        empty : [Int]
        assert empty.random() == none

    do # Test List.remove_at
        list := &[10, 20, 30, 40, 50]
        list.remove_at(2)
        assert list == [10, 30, 40, 50]
        
        list.remove_at(2, count=2)
        assert list == [10, 50]

    do # Test List.remove_item
        list := &[10, 20, 10, 20, 30]
        list.remove_item(10)
        assert list == [20, 20, 30]
        
        list.remove_item(20, max_count=1)
        assert list == [20, 30]

    do # Test List.reversed
        assert [10, 20, 30].reversed() == [30, 20, 10]

    do # Test List.sample
        _ := [10, 20, 30].sample(2, weights=[90%, 5%, 5%]) # E.g. [10, 10]

    do # Test List.shuffle
        nums := &[10, 20, 30, 40]
        nums.shuffle()
        # E.g. [20, 40, 10, 30]

    do # Test List.shuffled
        nums := [10, 20, 30, 40]
        _ := nums.shuffled()
        # E.g. [20, 40, 10, 30]

    do # Test List.slice
        assert [10, 20, 30, 40, 50].slice(2, 4) == [20, 30, 40]
        assert [10, 20, 30, 40, 50].slice(-3, -2) == [30, 40]

    do # Test List.sort
        list := &[40, 10, -30, 20]
        list.sort()
        assert list == [-30, 10, 20, 40]
        
        list.sort(func(a,b:&Int) a.abs() <> b.abs())
        assert list == [10, 20, -30, 40]

    do # Test List.sorted
        assert [40, 10, -30, 20].sorted() == [-30, 10, 20, 40]
        assert [40, 10, -30, 20].sorted(
           func(a,b:&Int) a.abs() <> b.abs()
        ) == [10, 20, -30, 40]

    do # Test List.to
        assert [10, 20, 30, 40, 50].to(3) == [10, 20, 30]
        assert [10, 20, 30, 40, 50].to(-2) == [10, 20, 30, 40]

    do # Test List.unique
        assert [10, 20, 10, 10, 30].unique() == {10, 20, 30}

    do # Test List.where
        assert ["BC", "ABC", "CD"].where(func(t:&Text) t.starts_with("A")) == 2
        assert ["BC", "ABC", "CD"].where(func(t:&Text) t.starts_with("X")) == none

    do # Test Num.abs
        assert (-3.5).abs() == 3.5

    do # Test Num.acos
        assert (0.0).acos().near(1.5707963267948966)

    do # Test Num.acosh
        assert (1.0).acosh() == 0

    do # Test Num.asin
        assert (0.5).asin().near(0.5235987755982989)

    do # Test Num.asinh
        assert (0.0).asinh() == 0

    do # Test Num.atan
        assert (1.0).atan().near(0.7853981633974483)

    do # Test Num.atan2
        assert Num.atan2(1, 1).near(0.7853981633974483)

    do # Test Num.atanh
        assert (0.5).atanh().near(0.5493061443340549)

    do # Test Num.cbrt
        assert (27.0).cbrt() == 3

    do # Test Num.ceil
        assert (3.2).ceil() == 4

    do # Test Num.clamped
        assert (2.5).clamped(5.5, 10.5) == 5.5

    do # Test Num.copysign
        assert (3.0).copysign(-1) == -3

    do # Test Num.cos
        assert (0.0).cos() == 1

    do # Test Num.cosh
        assert (0.0).cosh() == 1

    do # Test Num.erf
        assert (0.0).erf() == 0

    do # Test Num.erfc
        assert (0.0).erfc() == 1

    do # Test Num.exp
        assert (1.0).exp().near(2.718281828459045)

    do # Test Num.exp2
        assert (3.0).exp2() == 8

    do # Test Num.expm1
        assert (1.0).expm1().near(1.7182818284590453)

    do # Test Num.fdim
        assert (5.0).fdim(3) == 2

    do # Test Num.floor
        assert (3.7).floor() == 3

    do # Test Num.hypot
        assert Num.hypot(3, 4) == 5

    do # Test Num.is_between
        assert (7.5).is_between(1, 10) == yes
        assert (7.5).is_between(10, 1) == yes
        assert (7.5).is_between(100, 200) == no
        assert (7.5).is_between(1, 7.5) == yes

    do # Test Num.isfinite
        assert (1.0).isfinite() == yes
        assert Num.INF.isfinite() == no

    do # Test Num.isinf
        assert Num.INF.isinf() == yes
        assert (1.0).isinf() == no

    do # Test Num.j0
        assert (0.0).j0() == 1

    do # Test Num.j1
        assert (0.0).j1() == 0

    do # Test Num.log
        assert Num.E.log() == 1

    do # Test Num.log10
        assert (100.0).log10() == 2

    do # Test Num.log1p
        assert (1.0).log1p().near(0.6931471805599453)

    do # Test Num.log2
        assert (8.0).log2() == 3

    do # Test Num.logb
        assert (8.0).logb() == 3

    do # Test Num.mix
        assert (0.5).mix(10, 20) == 15
        assert (0.25).mix(10, 20) == 12.5

    do # Test Num.near
        assert (1.0).near(1.000000001) == yes
        assert (100.0).near(110, ratio=0.1) == yes
        assert (5.0).near(5.1, min_epsilon=0.1) == yes

    do # Test Num.nextafter
        assert (1.0).nextafter(1.1) == 1.0000000000000002

    do # Test Num.parse
        assert Num.parse("3.14") == 3.14
        assert Num.parse("1e3") == 1000
        assert Num.parse("1.5junk") == none
        remainder : Text
        assert Num.parse("1.5junk", &remainder) == 1.5
        assert remainder == "junk"

    do # Test Num.percent
        assert (0.5).percent() == "50%"
        assert (1./3.).percent(2) == "34%"
        assert (1./3.).percent(precision=0.0001) == "33.3333%"
        assert (1./3.).percent(precision=10.) == "30%"

    do # Test Num.rint
        assert (3.5).rint() == 4
        assert (2.5).rint() == 2

    do # Test Num.round
        assert (2.3).round() == 2
        assert (2.7).round() == 3

    do # Test Num.significand
        assert (1234.567).significand() == 1.2056318359375

    do # Test Num.sin
        assert (0.0).sin() == 0

    do # Test Num.sinh
        assert (0.0).sinh() == 0

    do # Test Num.sqrt
        assert (16.0).sqrt() == 4

    do # Test Num.tan
        assert (0.0).tan() == 0

    do # Test Num.tanh
        assert (0.0).tanh() == 0

    do # Test Num.tgamma
        assert (1.0).tgamma() == 1

    do # Test Num.trunc
        assert (3.7).trunc() == 3
        assert (-3.7).trunc() == -3

    do # Test Num.with_precision
        assert (0.1234567).with_precision(0.01) == 0.12
        assert (123456.).with_precision(100) == 123500
        assert (1234567.).with_precision(5) == 1234565

    do # Test Num.y0
        assert (1.0).y0().near(0.08825696421567698)

    do # Test Num.y1
        assert (1.0).y1().near(-0.7812128213002887)

    if no # Test Path.accessed
        assert (./file.txt).accessed() == Int64(1704221100)
        assert (./not-a-file).accessed() == none

    if no # Test Path.append
        (./log.txt).append("extra line\n")!

    if no # Test Path.append_bytes
        (./log.txt).append_bytes([104, 105])!

    if no # Test Path.base_name
        assert (./path/to/file.txt).base_name() == "file.txt"

    if no # Test Path.by_line
        # Safely handle file not being readable:
        if lines := (./file.txt).by_line()
            for line in lines
                say(line.upper())
        else
            say("Couldn't read file!")
        
        # Assume the file is readable and error if that's not the case:
        for line in (/dev/stdin).by_line()!
            say(line.upper())

    if no # Test Path.byte_writer
        write := (./file.txt).byte_writer()
        write("Hello\n".utf8())!
        write("world\n".utf8(), close=yes)!

    if no # Test Path.can_execute
        assert (/bin/sh).can_execute() == yes
        assert (/usr/include/stdlib.h).can_execute() == no
        assert (/non/existant/file).can_execute() == no

    if no # Test Path.can_read
        assert (/usr/include/stdlib.h).can_read() == yes
        assert (/etc/shadow).can_read() == no
        assert (/non/existant/file).can_read() == no

    if no # Test Path.can_write
        assert (/tmp).can_write() == yes
        assert (/etc/passwd).can_write() == no
        assert (/non/existant/file).can_write() == no

    if no # Test Path.changed
        assert (./file.txt).changed() == Int64(1704221100)
        assert (./not-a-file).changed() == none

    if no # Test Path.child
        assert (./directory).child("file.txt") == (./directory/file.txt)

    if no # Test Path.children
        assert (./directory).children(include_hidden=yes) == [(./directory/.git), (./directory/foo.txt)]

    if no # Test Path.create_directory
        (./new_directory).create_directory()!

    if no # Test Path.current_dir
        assert Path.current_dir() == (/home/user/tomo)

    if no # Test Path.each_child
        for child in dir.each_child()!
            say("Child: $child")

    if no # Test Path.exists
        assert (/).exists() == yes

    if no # Test Path.expand_home
        # Assume current user is 'user'
        assert (~/foo).expand_home() == (/home/user/foo)
        # No change
        assert (/foo).expand_home() == (/foo)

    if no # Test Path.extension
        assert (./file.tar.gz).extension() == "tar.gz"
        assert (./file.tar.gz).extension(full=no) == "gz"
        assert (/foo).extension() == ""
        assert (./.git).extension() == ""

    if no # Test Path.files
        assert (./directory).files(include_hidden=yes) == [(./directory/file1.txt), (./directory/file2.txt)]

    if no # Test Path.glob
        # Current directory includes: foo.txt, baz.txt, qux.jpg, .hidden
        assert (./*).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
        assert (./*.txt).glob() == [(./foo.txt), (./baz.txt)]
        assert (./*.{txt,jpg}).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
        assert (./.*).glob() == [(./.hidden)]
        
        # Globs with no matches return an empty list:
        assert (./*.xxx).glob() == []

    if no # Test Path.group
        assert (/bin).group() == "root"
        assert (/non/existent/file).group() == none

    if no # Test Path.has_extension
        assert (/foo.txt).has_extension("txt") == yes
        assert (/foo.txt).has_extension(".txt") == yes
        assert (/foo.tar.gz).has_extension("gz") == yes
        assert (/foo.tar.gz).has_extension("zip") == no

    if no # Test Path.is_directory
        assert (./directory/).is_directory() == yes
        assert (./file.txt).is_directory() == no

    if no # Test Path.is_file
        assert (./file.txt).is_file() == yes
        assert (./directory/).is_file() == no

    if no # Test Path.is_socket
        assert (./socket).is_socket() == yes

    if no # Test Path.is_symlink
        assert (./link).is_symlink() == yes

    if no # Test Path.lines
        lines := (./file.txt).lines()!

    if no # Test Path.matches_glob
        assert (./file.txt).matches_glob("*.txt")
        assert (./file.c).matches_glob("*.{c,h}")

    if no # Test Path.modified
        assert (./file.txt).modified() == Int64(1704221100)
        assert (./not-a-file).modified() == none

    if no # Test Path.move
        (./file.txt).move(/tmp/renamed.txt)!

    if no # Test Path.owner
        assert (/bin).owner() == "root"
        assert (/non/existent/file).owner() == none

    if no # Test Path.parent
        assert (./path/to/file.txt).parent() == (./path/to/)

    if no # Test Path.read
        assert (./hello.txt).read() == "Hello"
        assert (./nosuchfile.xxx).read() == none

    if no # Test Path.read_bytes
        assert (./hello.txt).read_bytes()! == [72, 101, 108, 108, 111]
        assert (./nosuchfile.xxx).read_bytes() == none

    if no # Test Path.relative_to
        assert (./path/to/file.txt).relative_to((./path)) == (./to/file.txt)
        assert (/tmp/foo).relative_to((/tmp)) == (./foo)

    if no # Test Path.remove
        (./file.txt).remove()!

    if no # Test Path.resolved
        assert (~/foo).resolved() == (/home/user/foo)
        assert (./path/to/file.txt).resolved(relative_to=(/foo)) == (/foo/path/to/file.txt)

    if no # Test Path.set_owner
        (./file.txt).set_owner(owner="root", group="wheel")!

    if no # Test Path.sibling
        assert (/foo/baz).sibling("doop") == (/foo/doop)

    if no # Test Path.subdirectories
        assert (./directory).subdirectories() == [(./directory/subdir1), (./directory/subdir2)]
        assert (./directory).subdirectories(include_hidden=yes) == [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]

    if no # Test Path.unique_directory
        created := (/tmp/my-dir.XXXXXX).unique_directory()
        assert created.is_directory() == yes
        created.remove()!

    if no # Test Path.walk
        for p in (/tmp).walk()
            say("File or dir: $p")
        
        # The path itself is always included:
        assert [p for p in (./file.txt).walk()] == [(./file.txt)]

    if no # Test Path.write
        (./file.txt).write("Hello, world!")!

    if no # Test Path.write_bytes
        (./file.txt).write_bytes([104, 105])!

    if no # Test Path.write_unique
        created := (./file-XXXXXX.txt).write_unique("Hello, world!")!
        assert created == (./file-27QHtq.txt)
        assert created.read()! == "Hello, world!"
        created.remove()!

    if no # Test Path.write_unique_bytes
        created := (./file-XXXXXX.txt).write_unique_bytes([1, 2, 3])!
        assert created == (./file-27QHtq.txt)
        assert created.read_bytes()! == [1, 2, 3]
        created.remove()!

    if no # Test Path.writer
        write := (./file.txt).writer()
        write("Hello\n")!
        write("world\n", close=yes)!

    do # Test Table.clear
        t := &{"A":1}
        t.clear()
        assert t[] == {}

    do # Test Table.difference
        t1 := {"A": 1, "B": 2, "C": 3}
        t2 := {"B": 2, "C":30, "D": 40}
        assert t1.difference(t2) == {"A": 1, "D": 40}

    do # Test Table.get
        t := {"A": 1, "B": 2}
        assert t.get("A") == 1
        assert t.get("????") == none
        assert t.get("A")! == 1
        assert t.get("????") or 0 == 0

    do # Test Table.get_or_set
        t := &{"A": @[1, 2, 3]; default=@[]}
        t.get_or_set("A").insert(4)
        t.get_or_set("B").insert(99)
        assert t["A"][] == [1, 2, 3, 4]
        assert t["B"][] == [99]
        assert t.get_or_set("C", @[0, 0, 0])[] == [0, 0, 0]

    do # Test Table.has
        assert {"A": 1, "B": 2}.has("A") == yes
        assert {"A": 1, "B": 2}.has("xxx") == no

    do # Test Table.intersection
        t1 := {"A": 1, "B": 2, "C": 3}
        t2 := {"B": 2, "C":30, "D": 40}
        assert t1.intersection(t2) == {"B": 2}

    do # Test Table.remove
        t := &{"A": 1, "B": 2}
        t.remove("A")
        assert t == {"B": 2}

    do # Test Table.set
        t := &{"A": 1, "B": 2}
        t.set("C", 3)
        assert t == {"A": 1, "B": 2, "C": 3}

    do # Test Table.with
        t := {"A": 1, "B": 2}
        assert t.with({"B": 20, "C": 30}) == {"A": 1, "B": 20, "C": 30}

    do # Test Table.with_fallback
        t := {"A": 1; fallback={"B": 2}}
        t2 := t.with_fallback({"B": 3})
        assert t2["B"] == 3
        t3 := t.with_fallback(none)
        assert t3["B"] == none

    do # Test Table.without
        t := {"A": 1, "B": 2, "C": 3}
        assert t.without({"B": 2, "C": 30, "D": 40}) == {"A": 1, "C": 3}

    do # Test Text.as_c_string
        assert "Hello".as_c_string() == CString("Hello")

    do # Test Text.at
        assert "Amélie".at(3) == "é"

    do # Test Text.by_line
        text := "
            line one
            line two
        "
        lines := [line for line in text.by_line()]
        assert lines == ["line one", "line two"]

    do # Test Text.by_split
        text := "one,two,three"
        chunks := [chunk for chunk in text.by_split(",")]
        assert chunks == ["one", "two", "three"]

    do # Test Text.by_split_any
        text := "one,two,;,three"
        chunks := [chunk for chunk in text.by_split_any(",;")]
        assert chunks == ["one", "two", "three"]

    do # Test Text.caseless_equals
        assert "A".caseless_equals("a") == yes
        
        # Turkish lowercase "I" is "ı" (dotless I), not "i"
        assert "I".caseless_equals("i", language="tr_TR") == no

    do # Test Text.codepoint_names
        assert "Amélie".codepoint_names() == [
            "LATIN CAPITAL LETTER A",
            "LATIN SMALL LETTER M",
            "LATIN SMALL LETTER E WITH ACUTE",
            "LATIN SMALL LETTER L",
            "LATIN SMALL LETTER I",
            "LATIN SMALL LETTER E",
        ]

    do # Test Text.distance
        assert "hello".distance("hello") == 0
        texts := &["goodbye", "hello", "hallo"]
        texts.sort(func(a,b:&Text) a.distance("hello") <> b.distance("hello"))
        assert texts == ["hello", "hallo", "goodbye"]

    do # Test Text.ends_with
        assert "hello world".ends_with("world") == yes
        remainder : Text
        assert "hello world".ends_with("world", &remainder) == yes
        assert remainder == "hello "

    do # Test Text.find
        assert "one two".find("one") == 1
        assert "one two".find("two") == 5
        assert "one two".find("three") == none
        assert "one two".find("o", start=2) == 7

    do # Test Text.from
        assert "hello".from(2) == "ello"
        assert "hello".from(-2) == "lo"

    do # Test Text.from_c_string
        assert Text.from_c_string(CString("Hello")) == "Hello"

    do # Test Text.from_codepoint_names
        text := Text.from_codepoint_names([
            "LATIN CAPITAL LETTER A WITH RING ABOVE",
            "LATIN SMALL LETTER K",
            "LATIN SMALL LETTER E",
        ])
        assert text == "Åke"

    do # Test Text.from_utf16
        assert Text.from_utf16([197, 107, 101]) == "Åke"
        assert Text.from_utf16([12371, 12435, 12395, 12385, 12399, 19990, 30028]) == "こんにちは世界"

    do # Test Text.from_utf32
        assert Text.from_utf32([197, 107, 101]) == "Åke"

    do # Test Text.from_utf8
        assert Text.from_utf8([195, 133, 107, 101]) == "Åke"

    do # Test Text.has
        assert "hello world".has("wo") == yes
        assert "hello world".has("xxx") == no

    do # Test Text.join
        assert ", ".join(["one", "two", "three"]) == "one, two, three"

    do # Test Text.left_pad
        assert "x".left_pad(5) == "    x"
        assert "x".left_pad(5, "ABC") == "ABCAx"

    do # Test Text.lines
        assert "one\ntwo\nthree".lines() == ["one", "two", "three"]
        assert "one\ntwo\nthree\n".lines() == ["one", "two", "three"]
        assert "one\ntwo\nthree\n\n".lines() == ["one", "two", "three", ""]
        assert "one\r\ntwo\r\nthree\r\n".lines() == ["one", "two", "three"]
        assert "".lines() == []

    do # Test Text.lower
        assert "AMÉLIE".lower() == "amélie"
        assert "I".lower(language="tr_TR") == "ı"

    do # Test Text.matches_glob
        assert "hello world".matches_glob("h* *d")

    do # Test Text.middle_pad
        assert "x".middle_pad(6) == "  x   "
        assert "x".middle_pad(10, "ABC") == "ABCAxABCAB"

    do # Test Text.quoted
        assert "one\ntwo".quoted() == "\"one\\ntwo\""

    do # Test Text.repeat
        assert "Abc".repeat(3) == "AbcAbcAbc"

    do # Test Text.replace
        assert "Hello world".replace("world", "there") == "Hello there"

    do # Test Text.reversed
        assert "Abc".reversed() == "cbA"

    do # Test Text.right_pad
        assert "x".right_pad(5) == "x    "
        assert "x".right_pad(5, "ABC") == "xABCA"

    do # Test Text.slice
        assert "hello".slice(2, 3) == "el"
        assert "hello".slice(to=-2) == "hell"
        assert "hello".slice(from=2) == "ello"

    do # Test Text.split
        assert "one,two,,three".split(",") == ["one", "two", "", "three"]
        assert "abc".split() == ["a", "b", "c"]

    do # Test Text.split_any
        assert "one, two,,three".split_any(", ") == ["one", "two", "three"]

    do # Test Text.starts_with
        assert "hello world".starts_with("hello") == yes
        remainder : Text
        assert "hello world".starts_with("hello", &remainder) == yes
        assert remainder == " world"

    do # Test Text.title
        assert "amélie".title() == "Amélie"
        
        # In Turkish, uppercase "i" is "İ"
        assert "i".title(language="tr_TR") == "İ"

    do # Test Text.to
        assert "goodbye".to(3) == "goo"
        assert "goodbye".to(-2) == "goodby"

    do # Test Text.translate
        text := "A <tag> & an ampersand".translate({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot",
            "'": "&#39;",
        })
        assert text == "A &lt;tag&gt; &amp; an ampersand"

    do # Test Text.trim
        assert "   x y z    \n".trim() == "x y z"
        assert "one,".trim(",") == "one"
        assert "   xyz   ".trim(right=no) == "xyz   "

    do # Test Text.upper
        assert "amélie".upper() == "AMÉLIE"
        
        # In Turkish, uppercase "i" is "İ"
        assert "i".upper(language="tr_TR") == "İ"

    do # Test Text.utf16
        assert "Åke".utf16() == [197, 107, 101]
        assert "こんにちは世界".utf16() == [12371, 12435, 12395, 12385, 12399, 19990, 30028]

    do # Test Text.utf32
        assert "Amélie".utf32() == [65, 109, 233, 108, 105, 101]

    do # Test Text.utf8
        assert "Amélie".utf8() == [65, 109, 195, 169, 108, 105, 101]

    do # Test Text.width
        assert "Amélie".width() == 6
        assert "🤠".width() == 2

    do # Test Text.without_prefix
        assert "foo:baz".without_prefix("foo:") == "baz"
        assert "qux".without_prefix("foo:") == "qux"

    do # Test Text.without_suffix
        assert "baz.foo".without_suffix(".foo") == "baz"
        assert "qux".without_suffix(".foo") == "qux"

    if no # Test ask
        assert ask("What's your name? ") == "Arthur Dent"

    if no # Test at_cleanup
        at_cleanup(func()
            _ := (/tmp/file.txt).remove(ignore_missing=yes)
        )

    if no # Test exit
        exit("Goodbye forever!", Int32(1))

    if no # Test fail
        fail("Oh no!")

    if no # Test getenv
        assert getenv("TERM") == "xterm-256color"
        assert getenv("not_a_variable") == none

    if no # Test print
        print("Hello ", newline=no)
        print("world!")

    if no # Test say
        say("Hello ", newline=no)
        say("world!")

    if no # Test setenv
        setenv("FOOBAR", "xyz")

    if no # Test sleep
        sleep(1.5)
||||||| 4384ecac
=======
# This file contains auto-generated tests from the examples in api/*.yaml

func main()
    do # Test Bool.parse
        assert Bool.parse("yes") == yes
        assert Bool.parse("no") == no
        assert Bool.parse("???") == none
        
        assert Bool.parse("yesJUNK") == none
        remainder : Text
        assert Bool.parse("yesJUNK", &remainder) == yes
        assert remainder == "JUNK"

    do # Test Byte.get_bit
        assert Byte(6).get_bit(1) == no
        assert Byte(6).get_bit(2) == yes
        assert Byte(6).get_bit(3) == yes
        assert Byte(6).get_bit(4) == no

    do # Test Byte.hex
        assert Byte(18).hex(prefix=yes) == "0x12"

    do # Test Byte.is_between
        assert Byte(7).is_between(1, 10) == yes
        assert Byte(7).is_between(10, 1) == yes
        assert Byte(7).is_between(100, 200) == no
        assert Byte(7).is_between(1, 7) == yes

    do # Test Byte.parse
        assert Byte.parse("5") == Byte(5)
        assert Byte.parse("asdf") == none
        assert Byte.parse("123xyz") == none
        
        remainder : Text
        assert Byte.parse("123xyz", remainder=&remainder) == Byte(123)
        assert remainder == "xyz"

    do # Test Byte.to
        iter := Byte(2).to(4)
        assert iter() == Byte(2)
        assert iter() == Byte(3)
        assert iter() == Byte(4)
        assert iter() == none
        
        assert [x for x in Byte(2).to(5)] == [Byte(2), Byte(3), Byte(4), Byte(5)]
        assert [x for x in Byte(5).to(2)] == [Byte(5), Byte(4), Byte(3), Byte(2)]
        assert [x for x in Byte(2).to(5, step=2)] == [Byte(2), Byte(4)]

    do # Test CString.as_text
        assert CString("Hello").as_text() == "Hello"

    do # Test CString.join
        assert CString(",").join([CString("a"), CString("b")]) == CString("a,b")

    do # Test Int.abs
        assert (-10).abs() == 10

    do # Test Int.choose
        assert 4.choose(2) == 6

    do # Test Int.clamped
        assert 2.clamped(5, 10) == 5

    do # Test Int.factorial
        assert 10.factorial() == 3628800

    do # Test Int.get_bit
        assert 6.get_bit(1) == no
        assert 6.get_bit(2) == yes
        assert 6.get_bit(3) == yes
        assert 6.get_bit(4) == no

    do # Test Int.hex
        assert 255.hex(digits=4, uppercase=yes, prefix=yes) == "0x00FF"

    do # Test Int.is_between
        assert 7.is_between(1, 10) == yes
        assert 7.is_between(10, 1) == yes
        assert 7.is_between(100, 200) == no
        assert 7.is_between(1, 7) == yes

    do # Test Int.is_prime
        assert 7.is_prime() == yes
        assert 6.is_prime() == no

    do # Test Int.next_prime
        assert 11.next_prime() == 13

    do # Test Int.octal
        assert 64.octal(digits=4, prefix=yes) == "0o0100"

    do # Test Int.onward
        nums : &[Int] = &[]
        for i in 5.onward()
            nums.insert(i)
            stop if i == 10
        assert nums[] == [5, 6, 7, 8, 9, 10]

    do # Test Int.parse
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

    do # Test Int.sqrt
        assert 16.sqrt() == 4
        assert 17.sqrt() == 4

    do # Test Int.to
        iter := 2.to(5)
        assert iter() == 2
        assert iter() == 3
        assert iter() == 4
        assert iter() == 5
        assert iter() == none
        
        assert [x for x in 2.to(5)] == [2, 3, 4, 5]
        assert [x for x in 5.to(2)] == [5, 4, 3, 2]
        assert [x for x in 2.to(5, step=2)] == [2, 4]

    do # Test List.binary_search
        assert [1, 3, 5, 7, 9].binary_search(5) == 3
        assert [1, 3, 5, 7, 9].binary_search(-999) == 1
        assert [1, 3, 5, 7, 9].binary_search(999) == 6

    do # Test List.by
        assert [1, 2, 3, 4, 5, 6].by(2) == [1, 3, 5]

    do # Test List.clear
        list := &[10, 20]
        list.clear()
        assert list[] == []

    do # Test List.counts
        assert [10, 20, 30, 30, 30].counts() == {10:1, 20:1, 30:3}

    do # Test List.find
        assert [10, 20, 30, 40, 50].find(20) == 2
        assert [10, 20, 30, 40, 50].find(9999) == none

    do # Test List.from
        assert [10, 20, 30, 40, 50].from(3) == [30, 40, 50]

    do # Test List.has
        assert [10, 20, 30].has(20) == yes

    do # Test List.heap_pop
        my_heap := &[30, 10, 20]
        my_heap.heapify()
        assert my_heap.heap_pop() == 10

    do # Test List.heap_push
        my_heap : &[Int]
        my_heap.heap_push(10)
        assert my_heap.heap_pop() == 10

    do # Test List.heapify
        my_heap := &[30, 10, 20]
        my_heap.heapify()

    do # Test List.insert
        list := &[10, 20]
        list.insert(30)
        assert list == [10, 20, 30]
        
        list.insert(999, at=2)
        assert list == [10, 999, 20, 30]

    do # Test List.insert_all
        list := &[10, 20]
        list.insert_all([30, 40])
        assert list == [10, 20, 30, 40]
        
        list.insert_all([99, 100], at=2)
        assert list == [10, 99, 100, 20, 30, 40]

    do # Test List.pop
        list := &[10, 20, 30, 40]
        
        assert list.pop() == 40
        assert list[] == [10, 20, 30]
        
        assert list.pop(index=2) == 20
        assert list[] == [10, 30]

    do # Test List.random
        nums := [10, 20, 30]
        pick := nums.random()!
        assert nums.has(pick)
        empty : [Int]
        assert empty.random() == none

    do # Test List.remove_at
        list := &[10, 20, 30, 40, 50]
        list.remove_at(2)
        assert list == [10, 30, 40, 50]
        
        list.remove_at(2, count=2)
        assert list == [10, 50]

    do # Test List.remove_item
        list := &[10, 20, 10, 20, 30]
        list.remove_item(10)
        assert list == [20, 20, 30]
        
        list.remove_item(20, max_count=1)
        assert list == [20, 30]

    do # Test List.reversed
        assert [10, 20, 30].reversed() == [30, 20, 10]

    do # Test List.sample
        _ := [10, 20, 30].sample(2, weights=[90%, 5%, 5%]) # E.g. [10, 10]

    do # Test List.shuffle
        nums := &[10, 20, 30, 40]
        nums.shuffle()
        # E.g. [20, 40, 10, 30]

    do # Test List.shuffled
        nums := [10, 20, 30, 40]
        _ := nums.shuffled()
        # E.g. [20, 40, 10, 30]

    do # Test List.slice
        assert [10, 20, 30, 40, 50].slice(2, 4) == [20, 30, 40]
        assert [10, 20, 30, 40, 50].slice(-3, -2) == [30, 40]

    do # Test List.sort
        list := &[40, 10, -30, 20]
        list.sort()
        assert list == [-30, 10, 20, 40]
        
        list.sort(func(a,b:&Int) a.abs() <> b.abs())
        assert list == [10, 20, -30, 40]

    do # Test List.sorted
        assert [40, 10, -30, 20].sorted() == [-30, 10, 20, 40]
        assert [40, 10, -30, 20].sorted(
           func(a,b:&Int) a.abs() <> b.abs()
        ) == [10, 20, -30, 40]

    do # Test List.to
        assert [10, 20, 30, 40, 50].to(3) == [10, 20, 30]
        assert [10, 20, 30, 40, 50].to(-2) == [10, 20, 30, 40]

    do # Test List.unique
        assert [10, 20, 10, 10, 30].unique() == {10, 20, 30}

    do # Test List.where
        assert ["BC", "ABC", "CD"].where(func(t:&Text) t.starts_with("A")) == 2
        assert ["BC", "ABC", "CD"].where(func(t:&Text) t.starts_with("X")) == none

    do # Test Num.abs
        assert (-3.5).abs() == 3.5

    do # Test Num.acos
        assert (0.0).acos().near(1.5707963267948966)

    do # Test Num.acosh
        assert (1.0).acosh() == 0

    do # Test Num.asin
        assert (0.5).asin().near(0.5235987755982989)

    do # Test Num.asinh
        assert (0.0).asinh() == 0

    do # Test Num.atan
        assert (1.0).atan().near(0.7853981633974483)

    do # Test Num.atan2
        assert Num.atan2(1, 1).near(0.7853981633974483)

    do # Test Num.atanh
        assert (0.5).atanh().near(0.5493061443340549)

    do # Test Num.cbrt
        assert (27.0).cbrt() == 3

    do # Test Num.ceil
        assert (3.2).ceil() == 4

    do # Test Num.clamped
        assert (2.5).clamped(5.5, 10.5) == 5.5

    do # Test Num.copysign
        assert (3.0).copysign(-1) == -3

    do # Test Num.cos
        assert (0.0).cos() == 1

    do # Test Num.cosh
        assert (0.0).cosh() == 1

    do # Test Num.erf
        assert (0.0).erf() == 0

    do # Test Num.erfc
        assert (0.0).erfc() == 1

    do # Test Num.exp
        assert (1.0).exp().near(2.718281828459045)

    do # Test Num.exp2
        assert (3.0).exp2() == 8

    do # Test Num.expm1
        assert (1.0).expm1().near(1.7182818284590453)

    do # Test Num.fdim
        assert (5.0).fdim(3) == 2

    do # Test Num.floor
        assert (3.7).floor() == 3

    do # Test Num.hypot
        assert Num.hypot(3, 4) == 5

    do # Test Num.is_between
        assert (7.5).is_between(1, 10) == yes
        assert (7.5).is_between(10, 1) == yes
        assert (7.5).is_between(100, 200) == no
        assert (7.5).is_between(1, 7.5) == yes

    do # Test Num.isfinite
        assert (1.0).isfinite() == yes
        assert Num.INF.isfinite() == no

    do # Test Num.isinf
        assert Num.INF.isinf() == yes
        assert (1.0).isinf() == no

    do # Test Num.j0
        assert (0.0).j0() == 1

    do # Test Num.j1
        assert (0.0).j1() == 0

    do # Test Num.log
        assert Num.E.log() == 1

    do # Test Num.log10
        assert (100.0).log10() == 2

    do # Test Num.log1p
        assert (1.0).log1p().near(0.6931471805599453)

    do # Test Num.log2
        assert (8.0).log2() == 3

    do # Test Num.logb
        assert (8.0).logb() == 3

    do # Test Num.mix
        assert (0.5).mix(10, 20) == 15
        assert (0.25).mix(10, 20) == 12.5

    do # Test Num.near
        assert (1.0).near(1.000000001) == yes
        assert (100.0).near(110, ratio=0.1) == yes
        assert (5.0).near(5.1, min_epsilon=0.1) == yes

    do # Test Num.nextafter
        assert (1.0).nextafter(1.1) == 1.0000000000000002

    do # Test Num.parse
        assert Num.parse("3.14") == 3.14
        assert Num.parse("1e3") == 1000
        assert Num.parse("1.5junk") == none
        remainder : Text
        assert Num.parse("1.5junk", &remainder) == 1.5
        assert remainder == "junk"

    do # Test Num.percent
        assert (0.5).percent() == "50%"
        assert (1./3.).percent(2) == "34%"
        assert (1./3.).percent(precision=0.0001) == "33.3333%"
        assert (1./3.).percent(precision=10.) == "30%"

    do # Test Num.rint
        assert (3.5).rint() == 4
        assert (2.5).rint() == 2

    do # Test Num.round
        assert (2.3).round() == 2
        assert (2.7).round() == 3

    do # Test Num.significand
        assert (1234.567).significand() == 1.2056318359375

    do # Test Num.sin
        assert (0.0).sin() == 0

    do # Test Num.sinh
        assert (0.0).sinh() == 0

    do # Test Num.sqrt
        assert (16.0).sqrt() == 4

    do # Test Num.tan
        assert (0.0).tan() == 0

    do # Test Num.tanh
        assert (0.0).tanh() == 0

    do # Test Num.tgamma
        assert (1.0).tgamma() == 1

    do # Test Num.trunc
        assert (3.7).trunc() == 3
        assert (-3.7).trunc() == -3

    do # Test Num.with_precision
        assert (0.1234567).with_precision(0.01) == 0.12
        assert (123456.).with_precision(100) == 123500
        assert (1234567.).with_precision(5) == 1234565

    do # Test Num.y0
        assert (1.0).y0().near(0.08825696421567698)

    do # Test Num.y1
        assert (1.0).y1().near(-0.7812128213002887)

    if no # Test Path.accessed
        assert (./file.txt).accessed() == Int64(1704221100)
        assert (./not-a-file).accessed() == none

    if no # Test Path.append
        (./log.txt).append("extra line\n")!

    if no # Test Path.append_bytes
        (./log.txt).append_bytes([104, 105])!

    if no # Test Path.base_name
        assert (./path/to/file.txt).base_name() == "file.txt"

    if no # Test Path.by_line
        # Safely handle file not being readable:
        if lines := (./file.txt).by_line()
            for line in lines
                say(line.upper())
        else
            say("Couldn't read file!")
        
        # Assume the file is readable and error if that's not the case:
        for line in (/dev/stdin).by_line()!
            say(line.upper())

    if no # Test Path.byte_writer
        write := (./file.txt).byte_writer()
        write("Hello\n".utf8())!
        write("world\n".utf8(), close=yes)!

    if no # Test Path.can_execute
        assert (/bin/sh).can_execute() == yes
        assert (/usr/include/stdlib.h).can_execute() == no
        assert (/non/existant/file).can_execute() == no

    if no # Test Path.can_read
        assert (/usr/include/stdlib.h).can_read() == yes
        assert (/etc/shadow).can_read() == no
        assert (/non/existant/file).can_read() == no

    if no # Test Path.can_write
        assert (/tmp).can_write() == yes
        assert (/etc/passwd).can_write() == no
        assert (/non/existant/file).can_write() == no

    if no # Test Path.changed
        assert (./file.txt).changed() == Int64(1704221100)
        assert (./not-a-file).changed() == none

    if no # Test Path.child
        assert (./directory).child("file.txt") == (./directory/file.txt)

    if no # Test Path.children
        assert (./directory).children(include_hidden=yes) == [(./directory/.git), (./directory/foo.txt)]

    if no # Test Path.create_directory
        (./new_directory).create_directory()!

    if no # Test Path.current_dir
        assert Path.current_dir() == (/home/user/tomo)

    if no # Test Path.each_child
        for child in (/dir).each_child()
            say("Child: $child")

    if no # Test Path.exists
        assert (/).exists() == yes

    if no # Test Path.expand_home
        # Assume current user is 'user'
        assert (~/foo).expand_home() == (/home/user/foo)
        # No change
        assert (/foo).expand_home() == (/foo)

    if no # Test Path.extension
        assert (./file.tar.gz).extension() == "tar.gz"
        assert (./file.tar.gz).extension(full=no) == "gz"
        assert (/foo).extension() == ""
        assert (./.git).extension() == ""

    if no # Test Path.files
        assert (./directory).files(include_hidden=yes) == [(./directory/file1.txt), (./directory/file2.txt)]

    if no # Test Path.glob
        # Current directory includes: foo.txt, baz.txt, qux.jpg, .hidden
        assert (./*).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
        assert (./*.txt).glob() == [(./foo.txt), (./baz.txt)]
        assert (./*.{txt,jpg}).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
        assert (./.*).glob() == [(./.hidden)]
        
        # Globs with no matches return an empty list:
        assert (./*.xxx).glob() == []

    if no # Test Path.group
        assert (/bin).group() == "root"
        assert (/non/existent/file).group() == none

    if no # Test Path.has_extension
        assert (/foo.txt).has_extension("txt") == yes
        assert (/foo.txt).has_extension(".txt") == yes
        assert (/foo.tar.gz).has_extension("gz") == yes
        assert (/foo.tar.gz).has_extension("zip") == no

    if no # Test Path.is_directory
        assert (./directory/).is_directory() == yes
        assert (./file.txt).is_directory() == no

    if no # Test Path.is_file
        assert (./file.txt).is_file() == yes
        assert (./directory/).is_file() == no

    if no # Test Path.is_socket
        assert (./socket).is_socket() == yes

    if no # Test Path.is_symlink
        assert (./link).is_symlink() == yes

    if no # Test Path.lines
        lines := (./file.txt).lines()!

    if no # Test Path.matches_glob
        assert (./file.txt).matches_glob("*.txt")
        assert (./file.c).matches_glob("*.{c,h}")

    if no # Test Path.modified
        assert (./file.txt).modified() == Int64(1704221100)
        assert (./not-a-file).modified() == none

    if no # Test Path.move
        (./file.txt).move(/tmp/renamed.txt)!

    if no # Test Path.owner
        assert (/bin).owner() == "root"
        assert (/non/existent/file).owner() == none

    if no # Test Path.parent
        assert (./path/to/file.txt).parent() == (./path/to/)

    if no # Test Path.read
        assert (./hello.txt).read() == "Hello"
        assert (./nosuchfile.xxx).read() == none

    if no # Test Path.read_bytes
        assert (./hello.txt).read_bytes()! == [72, 101, 108, 108, 111]
        assert (./nosuchfile.xxx).read_bytes() == none

    if no # Test Path.relative_to
        assert (./path/to/file.txt).relative_to((./path)) == (./to/file.txt)
        assert (/tmp/foo).relative_to((/tmp)) == (./foo)

    if no # Test Path.remove
        (./file.txt).remove()!

    if no # Test Path.resolved
        assert (~/foo).resolved() == (/home/user/foo)
        assert (./path/to/file.txt).resolved(relative_to=(/foo)) == (/foo/path/to/file.txt)

    if no # Test Path.set_owner
        (./file.txt).set_owner(owner="root", group="wheel")!

    if no # Test Path.sibling
        assert (/foo/baz).sibling("doop") == (/foo/doop)

    if no # Test Path.subdirectories
        assert (./directory).subdirectories() == [(./directory/subdir1), (./directory/subdir2)]
        assert (./directory).subdirectories(include_hidden=yes) == [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]

    if no # Test Path.unique_directory
        created := (/tmp/my-dir.XXXXXX).unique_directory()
        assert created.is_directory() == yes
        created.remove()!

    if no # Test Path.walk
        for p in (/tmp).walk()
            say("File or dir: $p")
        
        # The path itself is always included:
        assert [p for p in (./file.txt).walk()] == [(./file.txt)]

    if no # Test Path.write
        (./file.txt).write("Hello, world!")!

    if no # Test Path.write_bytes
        (./file.txt).write_bytes([104, 105])!

    if no # Test Path.write_unique
        created := (./file-XXXXXX.txt).write_unique("Hello, world!")!
        assert created == (./file-27QHtq.txt)
        assert created.read()! == "Hello, world!"
        created.remove()!

    if no # Test Path.write_unique_bytes
        created := (./file-XXXXXX.txt).write_unique_bytes([1, 2, 3])!
        assert created == (./file-27QHtq.txt)
        assert created.read_bytes()! == [1, 2, 3]
        created.remove()!

    if no # Test Path.writer
        write := (./file.txt).writer()
        write("Hello\n")!
        write("world\n", close=yes)!

    do # Test Table.clear
        t := &{"A":1}
        t.clear()
        assert t[] == {}

    do # Test Table.difference
        t1 := {"A": 1, "B": 2, "C": 3}
        t2 := {"B": 2, "C":30, "D": 40}
        assert t1.difference(t2) == {"A": 1, "D": 40}

    do # Test Table.get
        t := {"A": 1, "B": 2}
        assert t.get("A") == 1
        assert t.get("????") == none
        assert t.get("A")! == 1
        assert t.get("????") or 0 == 0

    do # Test Table.get_or_set
        t := &{"A": @[1, 2, 3]; default=@[]}
        t.get_or_set("A").insert(4)
        t.get_or_set("B").insert(99)
        assert t["A"][] == [1, 2, 3, 4]
        assert t["B"][] == [99]
        assert t.get_or_set("C", @[0, 0, 0])[] == [0, 0, 0]

    do # Test Table.has
        assert {"A": 1, "B": 2}.has("A") == yes
        assert {"A": 1, "B": 2}.has("xxx") == no

    do # Test Table.intersection
        t1 := {"A": 1, "B": 2, "C": 3}
        t2 := {"B": 2, "C":30, "D": 40}
        assert t1.intersection(t2) == {"B": 2}

    do # Test Table.remove
        t := &{"A": 1, "B": 2}
        t.remove("A")
        assert t == {"B": 2}

    do # Test Table.set
        t := &{"A": 1, "B": 2}
        t.set("C", 3)
        assert t == {"A": 1, "B": 2, "C": 3}

    do # Test Table.with
        t := {"A": 1, "B": 2}
        assert t.with({"B": 20, "C": 30}) == {"A": 1, "B": 20, "C": 30}

    do # Test Table.with_fallback
        t := {"A": 1; fallback={"B": 2}}
        t2 := t.with_fallback({"B": 3})
        assert t2["B"] == 3
        t3 := t.with_fallback(none)
        assert t3["B"] == none

    do # Test Table.without
        t := {"A": 1, "B": 2, "C": 3}
        assert t.without({"B": 2, "C": 30, "D": 40}) == {"A": 1, "C": 3}

    do # Test Text.as_c_string
        assert "Hello".as_c_string() == CString("Hello")

    do # Test Text.at
        assert "Amélie".at(3) == "é"

    do # Test Text.by_line
        text := "
            line one
            line two
        "
        lines := [line for line in text.by_line()]
        assert lines == ["line one", "line two"]

    do # Test Text.by_split
        text := "one,two,three"
        chunks := [chunk for chunk in text.by_split(",")]
        assert chunks == ["one", "two", "three"]

    do # Test Text.by_split_any
        text := "one,two,;,three"
        chunks := [chunk for chunk in text.by_split_any(",;")]
        assert chunks == ["one", "two", "three"]

    do # Test Text.caseless_equals
        assert "A".caseless_equals("a") == yes
        
        # Turkish lowercase "I" is "ı" (dotless I), not "i"
        assert "I".caseless_equals("i", language="tr_TR") == no

    do # Test Text.codepoint_names
        assert "Amélie".codepoint_names() == [
            "LATIN CAPITAL LETTER A",
            "LATIN SMALL LETTER M",
            "LATIN SMALL LETTER E WITH ACUTE",
            "LATIN SMALL LETTER L",
            "LATIN SMALL LETTER I",
            "LATIN SMALL LETTER E",
        ]

    do # Test Text.distance
        assert "hello".distance("hello") == 0
        texts := &["goodbye", "hello", "hallo"]
        texts.sort(func(a,b:&Text) a.distance("hello") <> b.distance("hello"))
        assert texts == ["hello", "hallo", "goodbye"]

    do # Test Text.ends_with
        assert "hello world".ends_with("world") == yes
        remainder : Text
        assert "hello world".ends_with("world", &remainder) == yes
        assert remainder == "hello "

    do # Test Text.find
        assert "one two".find("one") == 1
        assert "one two".find("two") == 5
        assert "one two".find("three") == none
        assert "one two".find("o", start=2) == 7

    do # Test Text.from
        assert "hello".from(2) == "ello"
        assert "hello".from(-2) == "lo"

    do # Test Text.from_c_string
        assert Text.from_c_string(CString("Hello")) == "Hello"

    do # Test Text.from_codepoint_names
        text := Text.from_codepoint_names([
            "LATIN CAPITAL LETTER A WITH RING ABOVE",
            "LATIN SMALL LETTER K",
            "LATIN SMALL LETTER E",
        ])
        assert text == "Åke"

    do # Test Text.from_utf16
        assert Text.from_utf16([197, 107, 101]) == "Åke"
        assert Text.from_utf16([12371, 12435, 12395, 12385, 12399, 19990, 30028]) == "こんにちは世界"

    do # Test Text.from_utf32
        assert Text.from_utf32([197, 107, 101]) == "Åke"

    do # Test Text.from_utf8
        assert Text.from_utf8([195, 133, 107, 101]) == "Åke"

    do # Test Text.has
        assert "hello world".has("wo") == yes
        assert "hello world".has("xxx") == no

    do # Test Text.join
        assert ", ".join(["one", "two", "three"]) == "one, two, three"

    do # Test Text.left_pad
        assert "x".left_pad(5) == "    x"
        assert "x".left_pad(5, "ABC") == "ABCAx"

    do # Test Text.lines
        assert "one\ntwo\nthree".lines() == ["one", "two", "three"]
        assert "one\ntwo\nthree\n".lines() == ["one", "two", "three"]
        assert "one\ntwo\nthree\n\n".lines() == ["one", "two", "three", ""]
        assert "one\r\ntwo\r\nthree\r\n".lines() == ["one", "two", "three"]
        assert "".lines() == []

    do # Test Text.lower
        assert "AMÉLIE".lower() == "amélie"
        assert "I".lower(language="tr_TR") == "ı"

    do # Test Text.matches_glob
        assert "hello world".matches_glob("h* *d")

    do # Test Text.middle_pad
        assert "x".middle_pad(6) == "  x   "
        assert "x".middle_pad(10, "ABC") == "ABCAxABCAB"

    do # Test Text.quoted
        assert "one\ntwo".quoted() == "\"one\\ntwo\""

    do # Test Text.repeat
        assert "Abc".repeat(3) == "AbcAbcAbc"

    do # Test Text.replace
        assert "Hello world".replace("world", "there") == "Hello there"

    do # Test Text.reversed
        assert "Abc".reversed() == "cbA"

    do # Test Text.right_pad
        assert "x".right_pad(5) == "x    "
        assert "x".right_pad(5, "ABC") == "xABCA"

    do # Test Text.slice
        assert "hello".slice(2, 3) == "el"
        assert "hello".slice(to=-2) == "hell"
        assert "hello".slice(from=2) == "ello"

    do # Test Text.split
        assert "one,two,,three".split(",") == ["one", "two", "", "three"]
        assert "abc".split() == ["a", "b", "c"]

    do # Test Text.split_any
        assert "one, two,,three".split_any(", ") == ["one", "two", "three"]

    do # Test Text.starts_with
        assert "hello world".starts_with("hello") == yes
        remainder : Text
        assert "hello world".starts_with("hello", &remainder) == yes
        assert remainder == " world"

    do # Test Text.title
        assert "amélie".title() == "Amélie"
        
        # In Turkish, uppercase "i" is "İ"
        assert "i".title(language="tr_TR") == "İ"

    do # Test Text.to
        assert "goodbye".to(3) == "goo"
        assert "goodbye".to(-2) == "goodby"

    do # Test Text.translate
        text := "A <tag> & an ampersand".translate({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot",
            "'": "&#39;",
        })
        assert text == "A &lt;tag&gt; &amp; an ampersand"

    do # Test Text.trim
        assert "   x y z    \n".trim() == "x y z"
        assert "one,".trim(",") == "one"
        assert "   xyz   ".trim(right=no) == "xyz   "

    do # Test Text.upper
        assert "amélie".upper() == "AMÉLIE"
        
        # In Turkish, uppercase "i" is "İ"
        assert "i".upper(language="tr_TR") == "İ"

    do # Test Text.utf16
        assert "Åke".utf16() == [197, 107, 101]
        assert "こんにちは世界".utf16() == [12371, 12435, 12395, 12385, 12399, 19990, 30028]

    do # Test Text.utf32
        assert "Amélie".utf32() == [65, 109, 233, 108, 105, 101]

    do # Test Text.utf8
        assert "Amélie".utf8() == [65, 109, 195, 169, 108, 105, 101]

    do # Test Text.width
        assert "Amélie".width() == 6
        assert "🤠".width() == 2

    do # Test Text.without_prefix
        assert "foo:baz".without_prefix("foo:") == "baz"
        assert "qux".without_prefix("foo:") == "qux"

    do # Test Text.without_suffix
        assert "baz.foo".without_suffix(".foo") == "baz"
        assert "qux".without_suffix(".foo") == "qux"

    if no # Test ask
        assert ask("What's your name? ") == "Arthur Dent"

    if no # Test at_cleanup
        at_cleanup(func()
            _ := (/tmp/file.txt).remove(ignore_missing=yes)
        )

    if no # Test exit
        exit("Goodbye forever!", Int32(1))

    if no # Test fail
        fail("Oh no!")

    if no # Test getenv
        assert getenv("TERM") == "xterm-256color"
        assert getenv("not_a_variable") == none

    if no # Test print
        print("Hello ", newline=no)
        print("world!")

    if no # Test say
        say("Hello ", newline=no)
        say("world!")

    if no # Test setenv
        setenv("FOOBAR", "xyz")

    if no # Test sleep
        sleep(1.5)
>>>>>>> hash-packages
