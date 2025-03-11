func main():
	str := "Hello Amélie!"
	!! Testing strings like $str

	>> str:upper()
	= "HELLO AMÉLIE!"
	>> str:lower()
	= "hello amélie!"
	>> str:lower():title()
	= "Hello Amélie!"
	>> str[1]
	= "H"

	>> "I":lower()
	= "i"
	>> "I":lower(language="tr_TR")
	= "ı"

	>> "i":upper()
	= "I"
	>> "i":upper(language="tr_TR")
	= "İ"

	>> "ian":title()
	= "Ian"
	>> "ian":title(language="tr_TR")
	= "İan"

	>> "I":caseless_equals("ı")
	= no
	>> "I":caseless_equals("ı", language="tr_TR")
	= yes

	>> str[9]
	= "é"

	>> \UE9
	= "é"

	>> \U65\U301
	= "é"

	>> \{Penguin}:codepoint_names()
	= ["PENGUIN"]

	>> \[31;1]
	= "$\e[31;1m"

	>> \UE9 == \U65\U301
	= yes

	amelie := "Am$(\UE9)lie"
	>> amelie:split()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie:utf32_codepoints()
	= [65, 109, 233, 108, 105, 101]
	>> amelie:bytes()
	= [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]
	>> Text.from_bytes([:Byte 0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65])!
	= "Amélie"
	>> Text.from_bytes([Byte(0xFF)])
	= none : Text?

	amelie2 := "Am$(\U65\U301)lie"
	>> amelie2:split()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie2:utf32_codepoints()
	= [65, 109, 233, 108, 105, 101]
	>> amelie2:bytes()
	= [0x41, 0x6D, 0xC3, 0xA9, 0x6C, 0x69, 0x65]

	>> amelie:codepoint_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
	>> amelie2:codepoint_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

	>> "Hello":replace($/e/, "X")
	= "HXllo"

	>> "Hello":has($/l/)
	= yes
	>> "Hello":has($/l{end}/)
	= no
	>> "Hello":has($/{start}l/)
	= no

	>> "Hello":has($/o/)
	= yes
	>> "Hello":has($/o{end}/)
	= yes
	>> "Hello":has($/{start}o/)
	= no

	>> "Hello":has($/H/)
	= yes
	>> "Hello":has($/H{end}/)
	= no
	>> "Hello":has($/{start}H/)
	= yes

	>> "Hello":replace($/l/, "")
	= "Heo"
	>> "xxxx":replace($/x/, "")
	= ""
	>> "xxxx":replace($/y/, "")
	= "xxxx"
	>> "One two three four five six":replace($/e /, "")
	= "Ontwo threfour fivsix"

	>> " one ":replace($/{start}{space}/, "")
	= "one "
	>> " one ":replace($/{space}{end}/, "")
	= " one"

	>> amelie:has($/$amelie2/)
	= yes

	>> multiline := "
		line one
		line two
	"
	= "line one$\nline two"

	!! Interpolation tests:
	>> "A $(1+2)"
	= "A 3"
	>> 'A $(1+2)'
	= 'A $(1+2)'
	>> `A $(1+2)`
	= "A 3"

	>> $"A $(1+2)"
	= "A 3"
	>> $$"A $(1+2)"
	= 'A $(1+2)'
	>> $="A =(1+2)"
	= "A 3"
	>> ${one {nested} two $(1+2)}
	= "one {nested} two 3"

	>> "one two three":replace($/{alpha}/, "")
	= "  "
	>> "one two three":replace($/{alpha}/, "word")
	= "word word word"

	c := "É̩"
	>> c:codepoint_names()
	= ["LATIN CAPITAL LETTER E WITH ACUTE", "COMBINING VERTICAL LINE BELOW"]
	>> c == Text.from_codepoint_names(c:codepoint_names())!
	= yes
	>> c == Text.from_codepoints(c:utf32_codepoints())
	= yes
	>> c == Text.from_bytes(c:bytes())!
	= yes

	>> "one$(\n)two$(\n)three":lines()
	= ["one", "two", "three"]
	>> "one$(\n)two$(\n)three$(\n)":lines()
	= ["one", "two", "three"]
	>> "one$(\n)two$(\n)three$(\n\n)":lines()
	= ["one", "two", "three", ""]
	>> "one$(\r\n)two$(\r\n)three$(\r\n)":lines()
	= ["one", "two", "three"]
	>> "":lines()
	= []

	!! Test splitting and joining text:
	>> "one two three":split($/ /)
	= ["one", "two", "three"]

	>> "one,two,three,":split($/,/)
	= ["one", "two", "three", ""]

	>> "one    two three":split($/{space}/)
	= ["one", "two", "three"]

	>> "abc":split($//)
	= ["a", "b", "c"]

	>> ", ":join(["one", "two", "three"])
	= "one, two, three"

	>> "":join(["one", "two", "three"])
	= "onetwothree"

	>> "+":join(["one"])
	= "one"

	>> "+":join([:Text])
	= ""

	>> "":split()
	= []

	!! Test text:find_all()
	>> " #one  #two #three   ":find_all($/#{alpha}/)
	= [Match(text="#one", index=2, captures=["one"]), Match(text="#two", index=8, captures=["two"]), Match(text="#three", index=13, captures=["three"])]

	>> " #one  #two #three   ":find_all($/#{!space}/)
	= [Match(text="#one", index=2, captures=["one"]), Match(text="#two", index=8, captures=["two"]), Match(text="#three", index=13, captures=["three"])]

	>> "    ":find_all($/{alpha}/)
	= []

	>> " foo(baz(), 1)  doop() ":find_all($/{id}(?)/)
	= [Match(text="foo(baz(), 1)", index=2, captures=["foo", "baz(), 1"]), Match(text="doop()", index=17, captures=["doop", ""])]

	>> "":find_all($Pattern'')
	= []

	>> "Hello":find_all($Pattern'')
	= []

	!! Test text:find()
	>> " one   two  three   ":find($/{id}/, start=-999)
	= none : Match?
	>> " one   two  three   ":find($/{id}/, start=999)
	= none : Match?
	>> " one   two  three   ":find($/{id}/)
	= Match(text="one", index=2, captures=["one"]) : Match?
	>> " one   two  three   ":find($/{id}/, start=5)
	= Match(text="two", index=8, captures=["two"]) : Match?

	!! Test text slicing:
	>> "abcdef":slice()
	= "abcdef"
	>> "abcdef":slice(from=3)
	= "cdef"
	>> "abcdef":slice(to=-2)
	= "abcde"
	>> "abcdef":slice(from=2, to=4)
	= "bcd"
	>> "abcdef":slice(from=5, to=1)
	= ""

	>> house := "家"
	= "家"
	>> house.length
	= 1
	>> house:codepoint_names()
	= ["CJK Unified Ideographs-5BB6"]
	>> house:utf32_codepoints()
	= [23478]

	>> "🐧":codepoint_names()
	= ["PENGUIN"]

	>> Text.from_codepoint_names(["not a valid name here buddy"])
	= none : Text?

	>> "one two; three four":find_all($/; {..}/)
	= [Match(text="; three four", index=8, captures=["three four"])]

	malicious := "{xxx}"
	>> $/$malicious/
	= $/{1{}xxx}/

	>> "Hello":replace($/{lower}/, "(\0)")
	= "H(ello)"

	>> " foo(xyz) foo(yyy) foo(z()) ":replace($/foo(?)/, "baz(\1)")
	= " baz(xyz) baz(yyy) baz(z()) "

	>> "<tag>":replace_all({$/</="&lt;", $/>/="&gt;"})
	= "&lt;tag&gt;"

	>> " BAD(x, fn(y), BAD(z), w) ":replace($/BAD(?)/, "good(\1)", recursive=yes)
	= " good(x, fn(y), good(z), w) "

	>> " BAD(x, fn(y), BAD(z), w) ":replace($/BAD(?)/, "good(\1)", recursive=no)
	= " good(x, fn(y), BAD(z), w) "

	>> "Hello":matches($/{id}/)
	= ["Hello"] : [Text]?
	>> "Hello":matches($/{lower}/)
	= none : [Text]?
	>> "Hello":matches($/{upper}/)
	= none : [Text]?
	>> "Hello...":matches($/{id}/)
	= none : [Text]?

	if matches := "hello world":matches($/{id} {id}/):
		>> matches
		= ["hello", "world"]
	else:
		fail("Failed to match")

	>> "hello world":map($/world/, func(m:Match): m.text:upper())
	= "hello WORLD"

	>> "Abc":repeat(3)
	= "AbcAbcAbc"

	>> "   abc def    ":trim()
	= "abc def"
	>> " abc123def ":trim($/{!digit}/)
	= "123"
	>> " abc123def ":trim($/{!digit}/, trim_left=no)
	= " abc123"
	>> " abc123def ":trim($/{!digit}/, trim_right=no)
	= "123def "
	# Only trim single whole matches that bookend the text:
	>> "AbcAbcxxxxxxxxAbcAbc":trim($/Abc/)
	= "AbcxxxxxxxxAbc"

	>> "A=B=C=D":replace($/{..}={..}/, "1:(\1) 2:(\2)")
	= "1:(A) 2:(B=C=D)"

	>> "abcde":starts_with("ab")
	= yes
	>> "abcde":starts_with("bc")
	= no

	>> "abcde":ends_with("de")
	= yes
	>> "abcde":starts_with("cd")
	= no

	>> ("hello" ++ " " ++ "Amélie"):reversed()
	= "eilémA olleh" : Text

	do:
		!! Testing concatenation-stability:
		ab := Text.from_codepoint_names(["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"])!
		>> ab:codepoint_names()
		= ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
		>> ab.length
		= 1

		a := Text.from_codepoint_names(["LATIN SMALL LETTER E"])!
		b := Text.from_codepoint_names(["COMBINING VERTICAL LINE BELOW"])!
		>> (a++b):codepoint_names()
		= ["LATIN SMALL LETTER E", "COMBINING VERTICAL LINE BELOW"]
		>> (a++b) == ab
		= yes
		>> (a++b).length
		= 1


	do:
		concat := "e" ++ Text.from_codepoints([Int32(0x300)])
		>> concat.length
		= 1

		concat2 := concat ++ Text.from_codepoints([Int32(0x302)])
		>> concat2.length
		= 1

		concat3 := concat2 ++ Text.from_codepoints([Int32(0x303)])
		>> concat3.length
		= 1

		final := Text.from_codepoints([Int32(0x65), Int32(0x300), Int32(0x302), Int32(0x303)])
		>> final.length
		= 1
		>> concat3 == final
		= yes

		concat4 := Text.from_codepoints([Int32(0x65), Int32(0x300)]) ++ Text.from_codepoints([Int32(0x302), Int32(0x303)])
		>> concat4.length
		= 1
		>> concat4 == final
		= yes

	>> "x":left_pad(5)
	= "    x"
	>> "x":right_pad(5)
	= "x    "
	>> "x":middle_pad(5)
	= "  x  "
	>> "1234":left_pad(8, "XYZ")
	= "XYZX1234" : Text
	>> "1234":right_pad(8, "XYZ")
	= "1234XYZX" : Text
	>> "1234":middle_pad(9, "XYZ")
	= "XY1234XYZ" : Text
