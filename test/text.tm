test "case conversion"
	>> str := "Hello Amélie!"
	>> str.upper()
	assert str.upper() == "HELLO AMÉLIE!"
	>> str.lower()
	assert str.lower() == "hello amélie!"
	>> str.lower().title()
	assert str.lower().title() == "Hello Amélie!"
	>> str[1]
	assert str[1] == "H"

test "language-specific case conversion"
	>> "I".lower()
	assert "I".lower() == "i"
	>> "I".lower(language="tr_TR")
	assert "I".lower(language="tr_TR") == "ı"
	>> "i".upper()
	assert "i".upper() == "I"
	>> "i".upper(language="tr_TR")
	assert "i".upper(language="tr_TR") == "İ"
	>> "ian".title()
	assert "ian".title() == "Ian"
	>> "ian".title(language="tr_TR")
	assert "ian".title(language="tr_TR") == "İan"
	>> "I".caseless_equals("ı")
	assert "I".caseless_equals("ı") == no
	>> "I".caseless_equals("ı", language="tr_TR")
	assert "I".caseless_equals("ı", language="tr_TR") == yes

test "indexing accented and out-of-bounds characters"
	>> str := "Hello Amélie!"
	>> str[9]
	assert str[9] == "é"
	>> str[99]
	assert str[99] == none

test "escape sequences"
	>> "\{UE9}"
	assert "\{UE9}" == "é"
	>> "\{U65}\{U301}"
	assert "\{U65}\{U301}" == "é"
	>> "\{Penguin}".codepoint_names()
	assert "\{Penguin}".codepoint_names() == ["PENGUIN"]
	>> "\[31;1]"
	assert "\[31;1]" == "\e[31;1m"
	assert "\{UE9}" == "\{U65}\{U301}"

test "grapheme splitting and utf encodings"
	>> amelie := "Am\{UE9}lie"
	>> amelie.split()
	assert amelie.split() == ["A", "m", "é", "l", "i", "e"]
	>> [c for c in amelie]
	assert [c for c in amelie] == ["A", "m", "é", "l", "i", "e"]
	>> amelie.utf32()
	assert amelie.utf32() == [65, 109, 233, 108, 105, 101]
	>> amelie.utf8()
	assert amelie.utf8() == [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]
	>> Text.from_utf8([0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65])
	assert Text.from_utf8([0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65])! == "Amélie"
	>> Text.from_utf8([Byte(0xFF)])
	assert Text.from_utf8([Byte(0xFF)]) == none

	>> amelie2 := "Am\{U65}\{U301}lie"
	>> amelie2.split()
	assert amelie2.split() == ["A", "m", "é", "l", "i", "e"]
	>> amelie2.utf32()
	assert amelie2.utf32() == [65, 109, 233, 108, 105, 101]
	>> amelie2.utf8()
	assert amelie2.utf8() == [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]

	>> amelie.codepoint_names()
	assert amelie.codepoint_names() == ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
	>> amelie2.codepoint_names()
	assert amelie2.codepoint_names() == ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

test "replace, has, and normalized equality"
	>> "Hello".replace("e", "X")
	assert "Hello".replace("e", "X") == "HXllo"
	>> "Hello".has("l")
	assert "Hello".has("l") == yes
	>> "Hello".has("x")
	assert "Hello".has("x") == no
	>> "Hello".replace("l", "")
	assert "Hello".replace("l", "") == "Heo"
	>> "xxxx".replace("x", "")
	assert "xxxx".replace("x", "") == ""
	>> "xxxx".replace("y", "")
	assert "xxxx".replace("y", "") == "xxxx"
	>> "One two three four five six".replace("e ", "")
	assert "One two three four five six".replace("e ", "") == "Ontwo threfour fivsix"
	>> "Hello".replace("", "xxx")
	assert "Hello".replace("", "xxx") == "Hello"
	>> "".replace("", "xxx")
	assert "".replace("", "xxx") == ""
	>> amelie := "Am\{UE9}lie"
	>> amelie2 := "Am\{U65}\{U301}lie"
	>> amelie.has(amelie2)
	assert amelie.has(amelie2) == yes

test "multiline text literals"
	>> multiline := "
		line one
		line two
	"
	assert multiline == "line one\nline two"

test "string interpolation"
	say("Interpolation tests:")
	>> "A $(1+2)"
	assert "A $(1+2)" == "A 3"
	>> "A \$(1+2)"
	assert "A \$(1+2)" == "A \$(1+2)"
	>> 'A $(1+2)'
	assert 'A $(1+2)' == "A 3"
	>> `A @(1+2)`
	assert `A @(1+2)` == "A 3"

test "combining marks and roundtrip conversions"
	>> c := "É̩"
	>> c.codepoint_names()
	assert c.codepoint_names() == ["LATIN CAPITAL LETTER E WITH ACUTE", "COMBINING VERTICAL LINE BELOW"]
	>> Text.from_codepoint_names(c.codepoint_names())
	assert c == Text.from_codepoint_names(c.codepoint_names())!
	>> Text.from_utf32(c.utf32())
	assert c == Text.from_utf32(c.utf32())!
	>> Text.from_utf8(c.utf8())
	assert c == Text.from_utf8(c.utf8())!

test "splitting into lines"
	>> "one\ntwo\nthree".lines()
	assert "one\ntwo\nthree".lines() == ["one", "two", "three"]
	>> "one\ntwo\nthree\n".lines()
	assert "one\ntwo\nthree\n".lines() == ["one", "two", "three"]
	>> "one\ntwo\nthree\n\n".lines()
	assert "one\ntwo\nthree\n\n".lines() == ["one", "two", "three", ""]
	>> "one\r\ntwo\r\nthree\r\n".lines()
	assert "one\r\ntwo\r\nthree\r\n".lines() == ["one", "two", "three"]
	>> "".lines()
	assert "".lines() == []

test "splitting and joining text"
	say("Test splitting and joining text:")
	>> "one,, two,three".split(",")
	assert "one,, two,three".split(",") == ["one", "", " two", "three"]
	>> [t for t in "one,, two,three".by_split(",")]
	assert [t for t in "one,, two,three".by_split(",")] == ["one", "", " two", "three"]
	>> "one,, two,three".split_any(", ")
	assert "one,, two,three".split_any(", ") == ["one", "two", "three"]
	>> [t for t in "one,, two,three".by_split_any(", ")]
	assert [t for t in "one,, two,three".by_split_any(", ")] == ["one", "two", "three"]
	>> ",one,, two,three,".split(",")
	assert ",one,, two,three,".split(",") == ["", "one", "", " two", "three", ""]
	>> [t for t in ",one,, two,three,".by_split(",")]
	assert [t for t in ",one,, two,three,".by_split(",")] == ["", "one", "", " two", "three", ""]
	>> ",one,, two,three,".split_any(", ")
	assert ",one,, two,three,".split_any(", ") == ["", "one", "two", "three", ""]
	>> [t for t in ",one,, two,three,".by_split_any(", ")]
	assert [t for t in ",one,, two,three,".by_split_any(", ")] == ["", "one", "two", "three", ""]
	>> "abc".split()
	assert "abc".split() == ["a", "b", "c"]
	>> "one two three".split_any()
	assert "one two three".split_any() == ["one", "two", "three"]
	>> ", ".join(["one", "two", "three"])
	assert ", ".join(["one", "two", "three"]) == "one, two, three"
	>> "".join(["one", "two", "three"])
	assert "".join(["one", "two", "three"]) == "onetwothree"
	>> "+".join(["one"])
	assert "+".join(["one"]) == "one"
	>> "+".join([])
	assert "+".join([]) == ""
	>> "".split()
	assert "".split() == []

test "text slicing"
	say("Test text slicing:")
	>> "abcdef".slice()
	assert "abcdef".slice() == "abcdef"
	>> "abcdef".slice(from=3)
	assert "abcdef".slice(from=3) == "cdef"
	>> "abcdef".slice(to=-2)
	assert "abcdef".slice(to=-2) == "abcde"
	>> "abcdef".slice(from=2, to=4)
	assert "abcdef".slice(from=2, to=4) == "bcd"
	>> "abcdef".slice(from=5, to=1)
	assert "abcdef".slice(from=5, to=1) == ""

test "CJK and emoji codepoints"
	>> house := "家"
	assert house == "家"
	>> house.length
	assert house.length == 1
	>> house.codepoint_names()
	assert house.codepoint_names() == ["CJK Unified Ideographs-5BB6"]
	>> house.utf32()
	assert house.utf32() == [23478]
	>> "🐧".codepoint_names()
	assert "🐧".codepoint_names() == ["PENGUIN"]
	>> Text.from_codepoint_names(["not a valid name here buddy"])
	assert Text.from_codepoint_names(["not a valid name here buddy"]) == none

test "replace, translate, repeat, affixes, and reversed"
	>> "Hello".replace("ello", "i")
	assert "Hello".replace("ello", "i") == "Hi"
	>> "<tag>".translate({"<": "&lt;", ">": "&gt;"})
	assert "<tag>".translate({"<": "&lt;", ">": "&gt;"}) == "&lt;tag&gt;"
	>> "Abc".repeat(3)
	assert "Abc".repeat(3) == "AbcAbcAbc"
	>> "abcde".starts_with("ab")
	assert "abcde".starts_with("ab") == yes
	>> "abcde".starts_with("bc")
	assert "abcde".starts_with("bc") == no
	>> "abcde".ends_with("de")
	assert "abcde".ends_with("de") == yes
	>> "abcde".starts_with("cd")
	assert "abcde".starts_with("cd") == no
	>> "abcde".without_prefix("ab")
	assert "abcde".without_prefix("ab") == "cde"
	>> "abcde".without_suffix("ab")
	assert "abcde".without_suffix("ab") == "abcde"
	>> "abcde".without_prefix("de")
	assert "abcde".without_prefix("de") == "abcde"
	>> "abcde".without_suffix("de")
	assert "abcde".without_suffix("de") == "abc"
	>> ("hello" ++ " " ++ "Amélie").reversed()
	assert ("hello" ++ " " ++ "Amélie").reversed() == "eilémA olleh"

test "concatenation stability"
	say("Testing concatenation-stability:")
	>> ab := Text.from_codepoint_names(["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"])!
	>> ab.codepoint_names()
	assert ab.codepoint_names() == ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
	>> ab.length
	assert ab.length == 1

	>> a := Text.from_codepoint_names(["LATIN SMALL LETTER E"])!
	>> b := Text.from_codepoint_names(["COMBINING VERTICAL LINE BELOW"])!
	>> (a++b).codepoint_names()
	assert (a++b).codepoint_names() == ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
	>> (a++b)
	assert (a++b) == ab
	>> (a++b).length
	assert (a++b).length == 1

test "combining mark concatenation lengths"
	>> concat := "e" ++ Text.from_utf32([Int32(0x300)])!
	>> concat.length
	assert concat.length == 1
	>> concat2 := concat ++ Text.from_utf32([Int32(0x302)])!
	>> concat2.length
	assert concat2.length == 1
	>> concat3 := concat2 ++ Text.from_utf32([Int32(0x303)])!
	>> concat3.length
	assert concat3.length == 1
	>> final := Text.from_utf32([Int32(0x65), Int32(0x300), Int32(0x302), Int32(0x303)])!
	>> final.length
	assert final.length == 1
	>> concat3
	assert concat3 == final
	>> concat4 := Text.from_utf32([Int32(0x65), Int32(0x300)])! ++ Text.from_utf32([Int32(0x302), Int32(0x303)])!
	>> concat4.length
	assert concat4.length == 1
	>> concat4
	assert concat4 == final

test "padding, width, and trimming"
	>> "x".left_pad(5)
	assert "x".left_pad(5) == "    x"
	>> "x".right_pad(5)
	assert "x".right_pad(5) == "x    "
	>> "x".middle_pad(5)
	assert "x".middle_pad(5) == "  x  "
	>> "1234".left_pad(8, "XYZ")
	assert "1234".left_pad(8, "XYZ") == "XYZX1234"
	>> "1234".right_pad(8, "XYZ")
	assert "1234".right_pad(8, "XYZ") == "1234XYZX"
	>> "1234".middle_pad(9, "XYZ")
	assert "1234".middle_pad(9, "XYZ") == "XY1234XYZ"
	>> amelie := "Am\{UE9}lie"
	>> amelie.width()
	assert amelie.width() == 6
	>> "   one,  ".trim(" ,")
	assert "   one,  ".trim(" ,") == "one"
	>> "   one,  ".trim(" ,", left=no)
	assert "   one,  ".trim(" ,", left=no) == "   one"
	>> "   one,  ".trim(" ,", right=no)
	assert "   one,  ".trim(" ,", right=no) == "one,  "
	>> "  ".trim(" ,")
	assert "  ".trim(" ,") == ""
	>> "  ".trim(" ,", left=no)
	assert "  ".trim(" ,", left=no) == ""

test "astral plane character encodings"
	>> test := "𤭢"
	>> test.utf32()
	assert test.utf32() == [150370]
	>> test.utf16()
	assert test.utf16() == [-10158, -8350]
	>> test.utf8()
	assert test.utf8() == [0xf0, 0xa4, 0xad, 0xa2]
	>> Text.from_utf32([150370])
	assert Text.from_utf32([150370]) == test
	>> Text.from_utf16([-10158, -8350])
	assert Text.from_utf16([-10158, -8350]) == test
	>> Text.from_utf8([0xf0, 0xa4, 0xad, 0xa2])
	assert Text.from_utf8([0xf0, 0xa4, 0xad, 0xa2]) == test

test "finding substrings"
	>> "one two".find("one")
	assert "one two".find("one") == 1
	>> "one two".find("two")
	assert "one two".find("two") == 5
	>> "one two".find("three")
	assert "one two".find("three") == none
	>> "one two".find("o", start=2)
	assert "one two".find("o", start=2) == 7

test "edit distance"
	>> "hello".distance("hello")
	assert "hello".distance("hello") == 0
	>> "hello".distance("goodbye")
	assert "hello".distance("goodbye") > 2.0
	>> "hello".distance("hola")
	assert "hello".distance("hola") < "hello".distance("goodbye")
	>> "hello".distance("Hello")
	assert "hello".distance("Hello") <= 1.0
	>> "hello".distance("xello")
	assert "hello".distance("xello") <= 1.0
	>> "hello".distance("ehllo")
	assert "hello".distance("ehllo") <= "hello".distance("XXllo")
	>> "shffle".distance("shuffle")
	assert "shffle".distance("shuffle") <= "shffle".distance("sample")

test "concatenating text with a non-text value is rejected"
	x := "hello" ++ 5
fails_compile "I don't know how to do operations between Text and Int"

test "skip and stop work in text loops"
	out := ""
	for c in "abcd"
		if c == "b"
			skip
		if c == "d"
			stop
		out ++= c
	assert out == "ac"
