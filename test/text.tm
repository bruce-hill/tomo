>> str := "Hello Amélie!"
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

>> amelie := "Amélie"
>> amelie:clusters()
= ["A", "m", "é", "l", "i", "e"] : [Text]
>> amelie:codepoints()
= [65_i32, 109_i32, 101_i32, 769_i32, 108_i32, 105_i32, 101_i32] : [Int32]
>> amelie:bytes()
= [65_i8, 109_i8, 101_i8, -52_i8, -127_i8, 108_i8, 105_i8, 101_i8] : [Int8]
