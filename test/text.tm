func main():
	>> str := "Hello Amélie!"
	//! Testing strings like $str

	>> str:upper()
	= "HELLO AMÉLIE!"
	>> str:lower()
	= "hello amélie!"
	>> str:lower():title()
	= "Hello Amélie!"


	>> \UE9
	= "é"

	>> \U65\U301
	= "é"

	>> \UE9 == \U65\U301
	= yes

	>> amelie := "Am$(\UE9)lie"
	>> amelie:split()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie:utf32_codepoints()
	= [65_i32, 109_i32, 233_i32, 108_i32, 105_i32, 101_i32] : [Int32]
	>> amelie:utf8_bytes()
	= [65_i8, 109_i8, -61_i8, -87_i8, 108_i8, 105_i8, 101_i8] : [Int8]

	>> amelie2 := "Am$(\U65\U301)lie"
	>> amelie2:split()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie2:utf32_codepoints()
	= [65_i32, 109_i32, 233_i32, 108_i32, 105_i32, 101_i32] : [Int32]
	>> amelie2:utf8_bytes()
	= [65_i8, 109_i8, -61_i8, -87_i8, 108_i8, 105_i8, 101_i8] : [Int8]

	>> amelie:codepoint_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
	>> amelie2:codepoint_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

	>> "Hello":replace("e", "X")
	= "HXllo"

	>> "Hello":has("l")
	= yes
	>> "Hello":has("l[..end]")
	= no
	>> "Hello":has("[..start]l")
	= no

	>> "Hello":has("o")
	= yes
	>> "Hello":has("o[..end]")
	= yes
	>> "Hello":has("[..start]o")
	= no

	>> "Hello":has("H")
	= yes
	>> "Hello":has("H[..end]")
	= no
	>> "Hello":has("[..start]H")
	= yes

	>> "Hello":replace("l", "")
	= "Heo"
	>> "xxxx":replace("x", "")
	= ""
	>> "xxxx":replace("y", "")
	= "xxxx"
	>> "One two three four five six":replace("e ", "")
	= "Ontwo threfour fivsix"

	>> " one ":replace("[..start][..space]", "")
	= "one "
	>> " one ":replace("[..space][..end]", "")
	= " one"

	>> amelie:has(amelie2)


	>> multiline := "
		line one
		line two
	"
	= "line one\nline two"

	//! Interpolation tests:
	>> "A $(1+2)"
	= "A 3"
	>> 'A $(1+2)'
	= "A $(1+2)"
	>> `A $(1+2)`
	= "A 3"

	>> $"A $(1+2)"
	= "A 3"
	>> $$"A $(1+2)"
	= "A $(1+2)"
	>> $="A =(1+2)"
	= "A 3"
	>> $(one (nested) two $(1+2))
	= "one (nested) two 3"

	>> "one two three":replace("[..alpha]", "")
	= "  "
	>> "one two three":replace("[..alpha]", "word")
	= "word word word"

	>> c := "É̩"
	>> c:codepoint_names()
	= ["LATIN CAPITAL LETTER E WITH ACUTE", "COMBINING VERTICAL LINE BELOW"]
	>> c == Text.from_codepoint_names(c:codepoint_names())
	= yes
	>> c == Text.from_codepoints(c:utf32_codepoints())
	= yes
	>> c == Text.from_bytes(c:utf8_bytes())
	= yes


	>> "one$(\n)two$(\n)three":lines()
	= ["one", "two", "three"]
	>> "one$(\n)two$(\n)three$(\n)":lines()
	= ["one", "two", "three"]
	>> "one$(\n)two$(\n)three$(\n\n)":lines()
	= ["one", "two", "three", ""]
	>> "one$(\r\n)two$(\r\n)three$(\r\n)":lines()
	= ["one", "two", "three"]

	//! Test splitting and joining text:
	>> "one two three":split(" ")
	= ["one", "two", "three"]

	>> "one,two,three,":split(",")
	= ["one", "two", "three", ""]

	>> "one    two three":split("[..space]")
	= ["one", "two", "three"]

	>> "abc":split("")
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

	//! Test text:find_all()
	>> " one  two three   ":find_all("[..alpha]")
	= ["one", "two", "three"]

	>> " one  two three   ":find_all("[..!space]")
	= ["one", "two", "three"]

	>> "    ":find_all("[..alpha]")
	= []

	>> " foo(baz(), 1)  doop() ":find_all("[..id](?)")
	= ["foo(baz(), 1)", "doop()"]

	>> "":find_all("")
	= []

	>> "Hello":find_all("")
	= []

	//! Test text:find()
	>> " one   two  three   ":find("[..id]", start=-999)
	= 0
	>> " one   two  three   ":find("[..id]", start=999)
	= 0
	>> " one   two  three   ":find("[..id]")
	= 2
	>> " one   two  three   ":find("[..id]", start=5)
	= 8

	>> len := 0_i64
	>> "   one  ":find("[..id]", length=&len)
	= 4
	>> len
	= 3_i64

	//! Test text slicing:
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
