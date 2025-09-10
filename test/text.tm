func main()
	str := "Hello Amélie!"
	say("Testing strings like $str")

	>> str.upper()
	= "HELLO AMÉLIE!"
	>> str.lower()
	= "hello amélie!"
	>> str.lower().title()
	= "Hello Amélie!"
	>> str[1]
	= "H"

	>> "I".lower()
	= "i"
	>> "I".lower(language="tr_TR")
	= "ı"

	>> "i".upper()
	= "I"
	>> "i".upper(language="tr_TR")
	= "İ"

	>> "ian".title()
	= "Ian"
	>> "ian".title(language="tr_TR")
	= "İan"

	>> "I".caseless_equals("ı")
	= no
	>> "I".caseless_equals("ı", language="tr_TR")
	= yes

	>> str[9]
	= "é"

	>> str[99]
	= none

	>> "\{UE9}"
	= "é"

	>> "\{U65}\{U301}"
	= "é"

	>> "\{Penguin}".codepoint_names()
	= ["PENGUIN"]

	>> "\[31;1]"
	= "\e[31;1m"

	assert "\{UE9}" == "\{U65}\{U301}"

	amelie := "Am\{UE9}lie"
	>> amelie.split()
	= ["A", "m", "é", "l", "i", "e"]
	>> amelie.utf32()
	= [65, 109, 233, 108, 105, 101]
	>> amelie.utf8()
	= [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]
	>> Text.from_utf8([0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65])!
	= "Amélie"
	>> Text.from_utf8([Byte(0xFF)])
	= none

	amelie2 := "Am\{U65}\{U301}lie"
	>> amelie2.split()
	= ["A", "m", "é", "l", "i", "e"]
	>> amelie2.utf32()
	= [65, 109, 233, 108, 105, 101]
	>> amelie2.utf8()
	= [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]

	>> amelie.codepoint_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
	>> amelie2.codepoint_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

	>> "Hello".replace("e", "X")
	= "HXllo"

	>> "Hello".has("l")
	= yes
	>> "Hello".has("x")
	= no

	>> "Hello".replace("l", "")
	= "Heo"
	>> "xxxx".replace("x", "")
	= ""
	>> "xxxx".replace("y", "")
	= "xxxx"
	>> "One two three four five six".replace("e ", "")
	= "Ontwo threfour fivsix"

	>> amelie.has(amelie2)
	= yes

	>> multiline := "
		line one
		line two
	"
	= "line one\nline two"

	say("Interpolation tests:")
	>> "A $(1+2)"
	= "A 3"
	>> "A \$(1+2)"
	= "A \$(1+2)"
	>> `A $(1+2)`
	= "A 3"

	>> $"A $(1+2)"
	= "A 3"
	>> $$"A $(1+2)"
	= "A \$(1+2)"
	>> $="A =(1+2)"
	= "A 3"
	>> ${one {nested} two $(1+2)}
	= "one {nested} two 3"

	c := "É̩"
	>> c.codepoint_names()
	= ["LATIN CAPITAL LETTER E WITH ACUTE", "COMBINING VERTICAL LINE BELOW"]
	assert c == Text.from_codepoint_names(c.codepoint_names())!
	assert c == Text.from_utf32(c.utf32())!
	assert c == Text.from_utf8(c.utf8())!

	>> "one\ntwo\nthree".lines()
	= ["one", "two", "three"]
	>> "one\ntwo\nthree\n".lines()
	= ["one", "two", "three"]
	>> "one\ntwo\nthree\n\n".lines()
	= ["one", "two", "three", ""]
	>> "one\r\ntwo\r\nthree\r\n".lines()
	= ["one", "two", "three"]
	>> "".lines()
	= []

	say("Test splitting and joining text:")
	>> "one,, two,three".split(",")
	= ["one", "", " two", "three"]
	>> [t for t in "one,, two,three".by_split(",")]
	= ["one", "", " two", "three"]
	>> "one,, two,three".split_any(", ")
	= ["one", "two", "three"]
	>> [t for t in "one,, two,three".by_split_any(", ")]
	= ["one", "two", "three"]
	>> ",one,, two,three,".split(",")
	= ["", "one", "", " two", "three", ""]
	>> [t for t in ",one,, two,three,".by_split(",")]
	= ["", "one", "", " two", "three", ""]
	>> ",one,, two,three,".split_any(", ")
	= ["", "one", "two", "three", ""]
	>> [t for t in ",one,, two,three,".by_split_any(", ")]
	= ["", "one", "two", "three", ""]

	>> "abc".split()
	= ["a", "b", "c"]

	>> "one two three".split_any()
	= ["one", "two", "three"]

	>> ", ".join(["one", "two", "three"])
	= "one, two, three"

	>> "".join(["one", "two", "three"])
	= "onetwothree"

	>> "+".join(["one"])
	= "one"

	>> "+".join([])
	= ""

	>> "".split()
	= []

	say("Test text slicing:")
	>> "abcdef".slice()
	= "abcdef"
	>> "abcdef".slice(from=3)
	= "cdef"
	>> "abcdef".slice(to=-2)
	= "abcde"
	>> "abcdef".slice(from=2, to=4)
	= "bcd"
	>> "abcdef".slice(from=5, to=1)
	= ""

	>> house := "家"
	= "家"
	>> house.length
	= 1
	>> house.codepoint_names()
	= ["CJK Unified Ideographs-5BB6"]
	>> house.utf32()
	= [23478]

	>> "🐧".codepoint_names()
	= ["PENGUIN"]

	>> Text.from_codepoint_names(["not a valid name here buddy"])
	= none

	>> "Hello".replace("ello", "i")
	= "Hi"

	>> "<tag>".translate({"<"="&lt;", ">"="&gt;"})
	= "&lt;tag&gt;"

	>> "Abc".repeat(3)
	= "AbcAbcAbc"

	>> "abcde".starts_with("ab")
	= yes
	>> "abcde".starts_with("bc")
	= no

	>> "abcde".ends_with("de")
	= yes
	>> "abcde".starts_with("cd")
	= no

	>> "abcde".without_prefix("ab")
	= "cde"
	>> "abcde".without_suffix("ab")
	= "abcde"

	>> "abcde".without_prefix("de")
	= "abcde"
	>> "abcde".without_suffix("de")
	= "abc"

	>> ("hello" ++ " " ++ "Amélie").reversed()
	= "eilémA olleh"

	do
		say("Testing concatenation-stability:")
		ab := Text.from_codepoint_names(["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"])!
		>> ab.codepoint_names()
		= ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
		>> ab.length
		= 1

		a := Text.from_codepoint_names(["LATIN SMALL LETTER E"])!
		b := Text.from_codepoint_names(["COMBINING VERTICAL LINE BELOW"])!
		>> (a++b).codepoint_names()
		= ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
		assert (a++b) == ab
		>> (a++b).length
		= 1


	do
		concat := "e" ++ Text.from_utf32([Int32(0x300)])!
		>> concat.length
		= 1

		concat2 := concat ++ Text.from_utf32([Int32(0x302)])!
		>> concat2.length
		= 1

		concat3 := concat2 ++ Text.from_utf32([Int32(0x303)])!
		>> concat3.length
		= 1

		final := Text.from_utf32([Int32(0x65), Int32(0x300), Int32(0x302), Int32(0x303)])!
		>> final.length
		= 1
		assert concat3 == final

		concat4 := Text.from_utf32([Int32(0x65), Int32(0x300)])! ++ Text.from_utf32([Int32(0x302), Int32(0x303)])!
		>> concat4.length
		= 1
		assert concat4 == final

	>> "x".left_pad(5)
	= "    x"
	>> "x".right_pad(5)
	= "x    "
	>> "x".middle_pad(5)
	= "  x  "
	>> "1234".left_pad(8, "XYZ")
	= "XYZX1234"
	>> "1234".right_pad(8, "XYZ")
	= "1234XYZX"
	>> "1234".middle_pad(9, "XYZ")
	= "XY1234XYZ"

	>> amelie.width()
	= 6

	# Unicode character width is somewhat platform dependent:
	# cowboy := "🤠"
	# >> cowboy.width()
	# = 2
	# >> cowboy.left_pad(4)
	# = "  🤠"
	# >> cowboy.right_pad(4)
	# = "🤠  "
	# >> cowboy.middle_pad(4)
	# = " 🤠 "

	>> "   one,  ".trim(" ,")
	= "one"
	>> "   one,  ".trim(" ,", left=no)
	= "   one"
	>> "   one,  ".trim(" ,", right=no)
	= "one,  "
	>> "  ".trim(" ,")
	= ""
	>> "  ".trim(" ,", left=no)
	= ""

	do
		test := "𤭢"
		assert test.utf32() == [150370]
		assert test.utf16() == [-10158, -8350]
		assert test.utf8() == [0xf0, 0xa4, 0xad, 0xa2]

		assert Text.from_utf32([150370]) == test
		assert Text.from_utf16([-10158, -8350]) == test
		assert Text.from_utf8([0xf0, 0xa4, 0xad, 0xa2]) == test
