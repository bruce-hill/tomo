use ./patterns.c

struct PatternMatch(text:Text, index:Int, captures:[Text])

lang Pat
    convert(text:Text -> Pat)
        return C_code:Pat(Pattern$escape_text(@text))

    convert(n:Int -> Pat)
        return Pat.from_text("$n")

extend Text
    func matches_pattern(text:Text, pattern:Pat -> Bool)
        return C_code:Bool(Pattern$matches(@text, @pattern))

    func pattern_captures(text:Text, pattern:Pat -> [Text]?)
        return C_code:[Text]?(Pattern$captures(@text, @pattern))

    func replace_pattern(text:Text, pattern:Pat, replacement:Text, backref="@", recursive=yes -> Text)
        return C_code:Text(Pattern$replace(@text, @pattern, @replacement, @backref, @recursive))

    func translate_patterns(text:Text, replacements:{Pat=Text}, backref="@", recursive=yes -> Text)
        return C_code:Text(Pattern$replace_all(@text, @replacements, @backref, @recursive))

    func has_pattern(text:Text, pattern:Pat -> Bool)
        return C_code:Bool(Pattern$has(@text, @pattern))

    func find_patterns(text:Text, pattern:Pat -> [PatternMatch])
        return C_code:[PatternMatch](Pattern$find_all(@text, @pattern))

    func by_pattern(text:Text, pattern:Pat -> func(->PatternMatch?))
        return C_code:func(->PatternMatch?)(Pattern$by_match(@text, @pattern))

    func each_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch), recursive=yes)
        C_code { Pattern$each(@text, @pattern, @fn, @recursive); }

    func map_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch -> Text), recursive=yes -> Text)
        return C_code:Text(Pattern$map(@text, @pattern, @fn, @recursive))

    func split_pattern(text:Text, pattern:Pat -> [Text])
        return C_code:[Text](Pattern$split(@text, @pattern))

    func by_pattern_split(text:Text, pattern:Pat -> func(->Text?))
        return C_code:func(->Text?)(Pattern$by_split(@text, @pattern))

    func trim_pattern(text:Text, pattern=$Pat"{space}", left=yes, right=yes -> Text)
        return C_code:Text(Pattern$trim(@text, @pattern, @left, @right))
