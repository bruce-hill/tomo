use ./patterns.c

struct PatternMatch(text:Text, index:Int, captures:[Text])

lang Pat
    convert(text:Text -> Pat)
        return C_code:Pat(Patternヽescape_text(@text))

    convert(n:Int -> Pat)
        return Pat.from_text("$n")

extend Text
    func matching_pattern(text:Text, pattern:Pat, pos:Int = 1 -> PatternMatch?)
        result : PatternMatch
        if C_code:Bool(Patternヽmatch_at(@text, @pattern, @pos, (void*)&@result))
            return result
        return none

    func matches_pattern(text:Text, pattern:Pat -> Bool)
        return C_code:Bool(Patternヽmatches(@text, @pattern))

    func pattern_captures(text:Text, pattern:Pat -> [Text]?)
        return C_code:[Text]?(Patternヽcaptures(@text, @pattern))

    func replace_pattern(text:Text, pattern:Pat, replacement:Text, backref="@", recursive=yes -> Text)
        return C_code:Text(Patternヽreplace(@text, @pattern, @replacement, @backref, @recursive))

    func translate_patterns(text:Text, replacements:{Pat=Text}, backref="@", recursive=yes -> Text)
        return C_code:Text(Patternヽreplace_all(@text, @replacements, @backref, @recursive))

    func has_pattern(text:Text, pattern:Pat -> Bool)
        return C_code:Bool(Patternヽhas(@text, @pattern))

    func find_patterns(text:Text, pattern:Pat -> [PatternMatch])
        return C_code:[PatternMatch](Patternヽfind_all(@text, @pattern))

    func by_pattern(text:Text, pattern:Pat -> func(->PatternMatch?))
        return C_code:func(->PatternMatch?)(Patternヽby_match(@text, @pattern))

    func each_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch), recursive=yes)
        C_code { Patternヽeach(@text, @pattern, @fn, @recursive); }

    func map_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch -> Text), recursive=yes -> Text)
        return C_code:Text(Patternヽmap(@text, @pattern, @fn, @recursive))

    func split_pattern(text:Text, pattern:Pat -> [Text])
        return C_code:[Text](Patternヽsplit(@text, @pattern))

    func by_pattern_split(text:Text, pattern:Pat -> func(->Text?))
        return C_code:func(->Text?)(Patternヽby_split(@text, @pattern))

    func trim_pattern(text:Text, pattern=$Pat"{space}", left=yes, right=yes -> Text)
        return C_code:Text(Patternヽtrim(@text, @pattern, @left, @right))

func main()
    >> "Hello world".matching_pattern($Pat'{id}')
    >> "...Hello world".matching_pattern($Pat'{id}')
# func main(pattern:Pat, input=(/dev/stdin))
#     for line in input.by_line()!
#         skip if not line.has_pattern(pattern)
#         pos := 1
#         for match in line.by_pattern(pattern)
#             say(line.slice(pos, match.index-1), newline=no)
#             say("\033[34;1m$(match.text)\033[m", newline=no)
#             pos = match.index + match.text.length
#         say(line.from(pos), newline=yes)
