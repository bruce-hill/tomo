func main()
	str := "Hello Amélie!"
	say("Testing strings like $str")

	assert str.upper() == "HELLO AMÉLIE!"
	assert str.lower() == "hello amélie!"
	assert str.lower().title() == "Hello Amélie!"
	assert str[1] == "H"

	assert "I".lower() == "i"
	assert "I".lower(language="tr_TR") == "ı"

	assert "i".upper() == "I"
	assert "i".upper(language="tr_TR") == "İ"

	assert "ian".title() == "Ian"
	assert "ian".title(language="tr_TR") == "İan"

	assert "I".caseless_equals("ı") == no
	assert "I".caseless_equals("ı", language="tr_TR") == yes

	assert str[9] == "é"

	assert str[99] == none

	assert "\{UE9}" == "é"

	assert "\{U65}\{U301}" == "é"

	assert "\{Penguin}".codepoint_names() == ["PENGUIN"]

	assert "\[31;1]" == "\e[31;1m"

	assert "\{UE9}" == "\{U65}\{U301}"

	amelie := "Am\{UE9}lie"
	assert amelie.split() == ["A", "m", "é", "l", "i", "e"]
	assert amelie.utf32() == [65, 109, 233, 108, 105, 101]
	assert amelie.utf8() == [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]
	assert Text.from_utf8([0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65])! == "Amélie"
	assert Text.from_utf8([Byte(0xFF)]) == none

	amelie2 := "Am\{U65}\{U301}lie"
	assert amelie2.split() == ["A", "m", "é", "l", "i", "e"]
	assert amelie2.utf32() == [65, 109, 233, 108, 105, 101]
	assert amelie2.utf8() == [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]

	assert amelie.codepoint_names() == ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
	assert amelie2.codepoint_names() == ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

	assert "Hello".replace("e", "X") == "HXllo"

	assert "Hello".has("l") == yes
	assert "Hello".has("x") == no

	assert "Hello".replace("l", "") == "Heo"
	assert "xxxx".replace("x", "") == ""
	assert "xxxx".replace("y", "") == "xxxx"
	assert "One two three four five six".replace("e ", "") == "Ontwo threfour fivsix"

	assert amelie.has(amelie2) == yes

	multiline := "
		line one
		line two
	"
	assert multiline == "line one\nline two"

	say("Interpolation tests:")
	assert "A $(1+2)" == "A 3"
	assert "A \$(1+2)" == "A \$(1+2)"
	assert 'A $(1+2)' == "A 3"
	assert `A @(1+2)` == "A 3"

	c := "É̩"
	assert c.codepoint_names() == ["LATIN CAPITAL LETTER E WITH ACUTE", "COMBINING VERTICAL LINE BELOW"]
	assert c == Text.from_codepoint_names(c.codepoint_names())!
	assert c == Text.from_utf32(c.utf32())!
	assert c == Text.from_utf8(c.utf8())!

	assert "one\ntwo\nthree".lines() == ["one", "two", "three"]
	assert "one\ntwo\nthree\n".lines() == ["one", "two", "three"]
	assert "one\ntwo\nthree\n\n".lines() == ["one", "two", "three", ""]
	assert "one\r\ntwo\r\nthree\r\n".lines() == ["one", "two", "three"]
	assert "".lines() == []

	say("Test splitting and joining text:")
	assert "one,, two,three".split(",") == ["one", "", " two", "three"]
	assert [t for t in "one,, two,three".by_split(",")] == ["one", "", " two", "three"]
	assert "one,, two,three".split_any(", ") == ["one", "two", "three"]
	assert [t for t in "one,, two,three".by_split_any(", ")] == ["one", "two", "three"]
	assert ",one,, two,three,".split(",") == ["", "one", "", " two", "three", ""]
	assert [t for t in ",one,, two,three,".by_split(",")] == ["", "one", "", " two", "three", ""]
	assert ",one,, two,three,".split_any(", ") == ["", "one", "two", "three", ""]
	assert [t for t in ",one,, two,three,".by_split_any(", ")] == ["", "one", "two", "three", ""]

	assert "abc".split() == ["a", "b", "c"]

	assert "one two three".split_any() == ["one", "two", "three"]

	assert ", ".join(["one", "two", "three"]) == "one, two, three"

	assert "".join(["one", "two", "three"]) == "onetwothree"

	assert "+".join(["one"]) == "one"

	assert "+".join([]) == ""

	assert "".split() == []

	say("Test text slicing:")
	assert "abcdef".slice() == "abcdef"
	assert "abcdef".slice(from=3) == "cdef"
	assert "abcdef".slice(to=-2) == "abcde"
	assert "abcdef".slice(from=2, to=4) == "bcd"
	assert "abcdef".slice(from=5, to=1) == ""

	house := "家"
	assert house == "家"
	assert house.length == 1
	assert house.codepoint_names() == ["CJK Unified Ideographs-5BB6"]
	assert house.utf32() == [23478]

	assert "🐧".codepoint_names() == ["PENGUIN"]

	assert Text.from_codepoint_names(["not a valid name here buddy"]) == none

	assert "Hello".replace("ello", "i") == "Hi"

	assert "<tag>".translate({"<": "&lt;", ">": "&gt;"}) == "&lt;tag&gt;"

	assert "Abc".repeat(3) == "AbcAbcAbc"

	assert "abcde".starts_with("ab") == yes
	assert "abcde".starts_with("bc") == no

	assert "abcde".ends_with("de") == yes
	assert "abcde".starts_with("cd") == no

	assert "abcde".without_prefix("ab") == "cde"
	assert "abcde".without_suffix("ab") == "abcde"

	assert "abcde".without_prefix("de") == "abcde"
	assert "abcde".without_suffix("de") == "abc"

	assert ("hello" ++ " " ++ "Amélie").reversed() == "eilémA olleh"

	do
		say("Testing concatenation-stability:")
		ab := Text.from_codepoint_names(["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"])!
		assert ab.codepoint_names() == ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
		assert ab.length == 1

		a := Text.from_codepoint_names(["LATIN SMALL LETTER E"])!
		b := Text.from_codepoint_names(["COMBINING VERTICAL LINE BELOW"])!
		assert (a++b).codepoint_names() == ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
		assert (a++b) == ab
		assert (a++b).length == 1


	do
		concat := "e" ++ Text.from_utf32([Int32(0x300)])!
		assert concat.length == 1

		concat2 := concat ++ Text.from_utf32([Int32(0x302)])!
		assert concat2.length == 1

		concat3 := concat2 ++ Text.from_utf32([Int32(0x303)])!
		assert concat3.length == 1

		final := Text.from_utf32([Int32(0x65), Int32(0x300), Int32(0x302), Int32(0x303)])!
		assert final.length == 1
		assert concat3 == final

		concat4 := Text.from_utf32([Int32(0x65), Int32(0x300)])! ++ Text.from_utf32([Int32(0x302), Int32(0x303)])!
		assert concat4.length == 1
		assert concat4 == final

	assert "x".left_pad(5) == "    x"
	assert "x".right_pad(5) == "x    "
	assert "x".middle_pad(5) == "  x  "
	assert "1234".left_pad(8, "XYZ") == "XYZX1234"
	assert "1234".right_pad(8, "XYZ") == "1234XYZX"
	assert "1234".middle_pad(9, "XYZ") == "XY1234XYZ"

	assert amelie.width() == 6

	assert "   one,  ".trim(" ,") == "one"
	assert "   one,  ".trim(" ,", left=no) == "   one"
	assert "   one,  ".trim(" ,", right=no) == "one,  "
	assert "  ".trim(" ,") == ""
	assert "  ".trim(" ,", left=no) == ""

	do
		test := "𤭢"
		assert test.utf32() == [150370]
		assert test.utf16() == [-10158, -8350]
		assert test.utf8() == [0xf0, 0xa4, 0xad, 0xa2]

		assert Text.from_utf32([150370]) == test
		assert Text.from_utf16([-10158, -8350]) == test
		assert Text.from_utf8([0xf0, 0xa4, 0xad, 0xa2]) == test
