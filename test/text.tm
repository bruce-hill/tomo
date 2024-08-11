func main():
	>> str := "Hello Amélie!"
	//! Testing strings like {str}

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

	>> amelie := "Am{\UE9}lie"
	>> amelie:clusters()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie:codepoints()
	= [65_i32, 109_i32, 101_i32, 769_i32, 108_i32, 105_i32, 101_i32] : [Int32]
	>> amelie:bytes()
	= [65_i8, 109_i8, 101_i8, -52_i8, -127_i8, 108_i8, 105_i8, 101_i8] : [Int8]
	>> #amelie
	= 6
	>> amelie:num_clusters()
	= 6
	>> amelie:num_codepoints()
	= 7
	>> amelie:num_bytes()
	= 8

	>> amelie2 := "Am{\U65\U301}lie"
	>> amelie2:clusters()
	= ["A", "m", "é", "l", "i", "e"] : [Text]
	>> amelie2:codepoints()
	= [65_i32, 109_i32, 101_i32, 769_i32, 108_i32, 105_i32, 101_i32] : [Int32]
	>> amelie2:bytes()
	= [65_i8, 109_i8, 101_i8, -52_i8, -127_i8, 108_i8, 105_i8, 101_i8] : [Int8]
	>> #amelie
	= 6
	>> amelie2:num_clusters()
	= 6
	>> amelie2:num_codepoints()
	= 7
	>> amelie2:num_bytes()
	= 8

	>> amelie:character_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E", "COMBINING ACUTE ACCENT", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
	>> amelie2:character_names()
	= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E", "COMBINING ACUTE ACCENT", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

	>> "Hello":replace("e", "X")
	= "HXllo"

	>> "Hello":has("l")
	= yes
	>> "Hello":has("l", End)
	= no
	>> "Hello":has("l", Start)
	= no

	>> "Hello":has("o")
	= yes
	>> "Hello":has("o", where=End)
	= yes
	>> "Hello":has("o", where=Start)
	= no

	>> "Hello":has("H")
	= yes
	>> "Hello":has("H", End)
	= no
	>> "Hello":has("H", Start)
	= yes

	>> "Hello":without("l")
	= "Heo"
	>> "xxxx":without("x")
	= ""
	>> "xxxx":without("y")
	= "xxxx"
	>> "One two three four five six":without("e ")
	= "Ontwo threfour fivsix"

	>> " one ":trimmed()
	= "one"
	>> " one ":trimmed(" aeiou")
	= "n"

	>> amelie:has(amelie2)


	>> multiline := "
		line one
		line two
	"
	= "line one\nline two"

