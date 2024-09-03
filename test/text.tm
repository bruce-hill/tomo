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
	>> amelie:clusters()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie:utf32_codepoints()
	= [65_i32, 109_i32, 233_i32, 108_i32, 105_i32, 101_i32] : [Int32]
	>> amelie:utf8_bytes()
	= [65_i8, 109_i8, -61_i8, -87_i8, 108_i8, 105_i8, 101_i8] : [Int8]

	>> amelie2 := "Am$(\U65\U301)lie"
	>> amelie2:clusters()
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

	>> c := "É̩"
	>> c:codepoint_names()
	= ["LATIN CAPITAL LETTER E WITH ACUTE", "COMBINING VERTICAL LINE BELOW"]
	>> c == Text.from_codepoint_names(c:codepoint_names())
	= yes
	>> c == Text.from_codepoints(c:utf32_codepoints())
	= yes
	>> c == Text.from_bytes(c:utf8_bytes())
	= yes


