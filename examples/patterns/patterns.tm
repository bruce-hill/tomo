use ./patterns.c

struct PatternMatch(text:Text, index:Int, captures:[Text])

lang Pat
    convert(text:Text -> Pat)
        return inline C : Pat { Pattern$escape_text(_$text); }

    convert(n:Int -> Pat)
        return Pat.from_text("$n")

extend Text
    func matches_pattern(text:Text, pattern:Pat -> Bool)
        return inline C : Bool { Pattern$matches(_$text, _$pattern); }

    func pattern_captures(text:Text, pattern:Pat -> [Text]?)
        return inline C : [Text]? { Pattern$captures(_$text, _$pattern); }

    func replace_pattern(text:Text, pattern:Pat, replacement:Text, backref="@", recursive=yes -> Text)
        return inline C : Text { Pattern$replace(_$text, _$pattern, _$replacement, _$backref, _$recursive); }

    func translate_patterns(text:Text, replacements:{Pat=Text}, backref="@", recursive=yes -> Text)
        return inline C : Text { Pattern$replace_all(_$text, _$replacements, _$backref, _$recursive); }

    func has_pattern(text:Text, pattern:Pat -> Bool)
        return inline C : Bool { Pattern$has(_$text, _$pattern); }

    func find_patterns(text:Text, pattern:Pat -> [PatternMatch])
        return inline C : [PatternMatch] { Pattern$find_all(_$text, _$pattern); }

    func by_pattern(text:Text, pattern:Pat -> func(->PatternMatch?))
        return inline C : func(->PatternMatch?) { Pattern$by_match(_$text, _$pattern); }

    func each_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch), recursive=yes)
        inline C { Pattern$each(_$text, _$pattern, _$fn, _$recursive); }

    func map_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch -> Text), recursive=yes -> Text)
        return inline C : Text { Pattern$map(_$text, _$pattern, _$fn, _$recursive); }

    func split_pattern(text:Text, pattern:Pat -> [Text])
        return inline C : [Text] { Pattern$split(_$text, _$pattern); }

    func by_pattern_split(text:Text, pattern:Pat -> func(->Text?))
        return inline C : func(->Text?) { Pattern$by_split(_$text, _$pattern); }

    func trim_pattern(text:Text, pattern=$Pat"{space}", left=yes, right=yes -> Text)
        return inline C : Text { Pattern$trim(_$text, _$pattern, _$left, _$right); }
