use patterns

func main()
	amelie := "Am\{UE9}lie"
	amelie2 := "Am\{U65}\{U301}lie"
	>> "Hello".replace_pattern($Pat/e/, "X")
	= "HXllo"

	>> "Hello".has_pattern($Pat/l/)
	= yes
	>> "Hello".has_pattern($Pat/l{end}/)
	= no
	>> "Hello".has_pattern($Pat/{start}l/)
	= no

	>> "Hello".has_pattern($Pat/o/)
	= yes
	>> "Hello".has_pattern($Pat/o{end}/)
	= yes
	>> "Hello".has_pattern($Pat/{start}o/)
	= no

	>> "Hello".has_pattern($Pat/H/)
	= yes
	>> "Hello".has_pattern($Pat/H{end}/)
	= no
	>> "Hello".has_pattern($Pat/{start}H/)
	= yes

	>> "Hello".replace_pattern($Pat/l/, "")
	= "Heo"
	>> "xxxx".replace_pattern($Pat/x/, "")
	= ""
	>> "xxxx".replace_pattern($Pat/y/, "")
	= "xxxx"
	>> "One two three four five six".replace_pattern($Pat/e /, "")
	= "Ontwo threfour fivsix"

	>> " one ".replace_pattern($Pat/{start}{space}/, "")
	= "one "
	>> " one ".replace_pattern($Pat/{space}{end}/, "")
	= " one"

	>> amelie.has_pattern($Pat/$amelie2/)
	= yes

	>> "one two three".replace_pattern($Pat/{alpha}/, "")
	= "  "
	>> "one two three".replace_pattern($Pat/{alpha}/, "word")
	= "word word word"

	say("Test splitting and joining text:")

	>> "one two three".split_pattern($Pat/ /)
	= ["one", "two", "three"]

	>> "one,two,three,".split_pattern($Pat/,/)
	= ["one", "two", "three", ""]

	>> "one    two three".split_pattern($Pat/{space}/)
	= ["one", "two", "three"]

	>> "abc".split_pattern($Pat//)
	= ["a", "b", "c"]

	>> ", ".join(["one", "two", "three"])
	= "one, two, three"

	>> "".join(["one", "two", "three"])
	= "onetwothree"

	>> "+".join(["one"])
	= "one"

	>> "+".join([])
	= ""

	say("Test text.find_patterns()")
	>> " #one  #two #three   ".find_patterns($Pat/#{alpha}/)
	= [PatternMatch(text="#one", index=2, captures=["one"]), PatternMatch(text="#two", index=8, captures=["two"]), PatternMatch(text="#three", index=13, captures=["three"])]

	>> " #one  #two #three   ".find_patterns($Pat/#{!space}/)
	= [PatternMatch(text="#one", index=2, captures=["one"]), PatternMatch(text="#two", index=8, captures=["two"]), PatternMatch(text="#three", index=13, captures=["three"])]

	>> "    ".find_patterns($Pat/{alpha}/)
	= []

	>> " foo(baz(), 1)  doop() ".find_patterns($Pat/{id}(?)/)
	= [PatternMatch(text="foo(baz(), 1)", index=2, captures=["foo", "baz(), 1"]), PatternMatch(text="doop()", index=17, captures=["doop", ""])]

	>> "".find_patterns($Pat'')
	= []

	>> "Hello".find_patterns($Pat'')
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
	>> house.utf32_codepoints()
	= [23478]

	>> "🐧".codepoint_names()
	= ["PENGUIN"]

	>> Text.from_codepoint_names(["not a valid name here buddy"])
	= none

	>> "one two; three four".find_patterns($Pat/; {..}/)
	= [PatternMatch(text="; three four", index=8, captures=["three four"])]

	malicious := "{xxx}"
	>> $Pat/$malicious/
	= $Pat/{1{}xxx}/

	>> "Hello".replace_pattern($Pat/{lower}/, "(@0)")
	= "H(ello)"

	>> " foo(xyz) foo(yyy) foo(z()) ".replace_pattern($Pat/foo(?)/, "baz(@1)")
	= " baz(xyz) baz(yyy) baz(z()) "

	>> "<tag>".translate_patterns({$Pat/</="&lt;", $Pat/>/="&gt;"})
	= "&lt;tag&gt;"

	>> " BAD(x, fn(y), BAD(z), w) ".replace_pattern($Pat/BAD(?)/, "good(@1)", recursive=yes)
	= " good(x, fn(y), good(z), w) "

	>> " BAD(x, fn(y), BAD(z), w) ".replace_pattern($Pat/BAD(?)/, "good(@1)", recursive=no)
	= " good(x, fn(y), BAD(z), w) "

	>> "Hello".matches_pattern($Pat/{id}/)
	= yes
	>> "Hello".matches_pattern($Pat/{lower}/)
	= no
	>> "Hello".matches_pattern($Pat/{upper}/)
	= no
	>> "Hello...".matches_pattern($Pat/{id}/)
	= no

	>> "hello world".map_pattern($Pat/world/, func(m:PatternMatch) m.text.upper())
	= "hello WORLD"

	>> "Abc".repeat(3)
	= "AbcAbcAbc"

	>> "   abc def    ".trim_pattern()
	= "abc def"
	>> " abc123def ".trim_pattern($Pat/{!digit}/)
	= "123"
	>> " abc123def ".trim_pattern($Pat/{!digit}/, left=no)
	= " abc123"
	>> " abc123def ".trim_pattern($Pat/{!digit}/, right=no)
	= "123def "
	# Only trim single whole matches that bookend the text:
	>> "AbcAbcxxxxxxxxAbcAbc".trim_pattern($Pat/Abc/)
	= "AbcxxxxxxxxAbc"

	>> "A=B=C=D".replace_pattern($Pat/{..}={..}/, "1:(@1) 2:(@2)")
	= "1:(A) 2:(B=C=D)"

	>> "abcde".starts_with("ab")
	= yes
	>> "abcde".starts_with("bc")
	= no

	>> "abcde".ends_with("de")
	= yes
	>> "abcde".starts_with("cd")
	= no

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
		>> (a++b) == ab
		= yes
		>> (a++b).length
		= 1


	do
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
	cowboy := "🤠"
	>> cowboy.width()
	= 2
	>> cowboy.left_pad(4)
	= "  🤠"
	>> cowboy.right_pad(4)
	= "🤠  "
	>> cowboy.middle_pad(4)
	= " 🤠 "

