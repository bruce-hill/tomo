use ./patterns.c

struct PatternMatch(text:Text, index:Int, captures:[Text])

lang Pat:
    convert(text:Text -> Pat):
        return inline C : Pat { Pattern$escape_text(_$text); }

    convert(n:Int -> Pat):
        return Pat.from_text("$n")

extend Text:
    func matches(text:Text, pattern:Pat -> [Text]?):
        return inline C : [Text]? { Pattern$matches(_$text, _$pattern); }

    func pat_replace(text:Text, pattern:Pat, replacement:Text, backref="@", recursive=yes -> Text):
        return inline C : Text { Pattern$replace(_$text, _$pattern, _$replacement, _$backref, _$recursive); }

    func pat_replace_all(text:Text, replacements:{Pat,Text}, backref="@", recursive=yes -> Text):
        return inline C : Text { Pattern$replace_all(_$text, _$replacements, _$backref, _$recursive); }

    func has(text:Text, pattern:Pat -> Bool):
        return inline C : Bool { Pattern$has(_$text, _$pattern); }

    func find_all(text:Text, pattern:Pat -> [PatternMatch]):
        return inline C : [PatternMatch] { Pattern$find_all(_$text, _$pattern); }

    func by_match(text:Text, pattern:Pat -> func(->PatternMatch?)):
        return inline C : func(->PatternMatch?) { Pattern$by_match(_$text, _$pattern); }

    func each(text:Text, pattern:Pat, fn:func(m:PatternMatch), recursive=yes):
        inline C { Pattern$each(_$text, _$pattern, _$fn, _$recursive); }

    func map(text:Text, pattern:Pat, fn:func(m:PatternMatch -> Text), recursive=yes -> Text):
        return inline C : Text { Pattern$map(_$text, _$pattern, _$fn, _$recursive); }

    func split(text:Text, pattern:Pat -> [Text]):
        return inline C : [Text] { Pattern$split(_$text, _$pattern); }

    func by_split(text:Text, pattern:Pat -> func(->Text?)):
        return inline C : func(->Text?) { Pattern$by_split(_$text, _$pattern); }

    func trim(text:Text, pattern:Pat, trim_left=yes, trim_right=yes -> Text):
        return inline C : Text { Pattern$trim(_$text, _$pattern, _$trim_left, _$trim_right); }

    func trim_left(text:Text, pattern:Pat -> Text):
        return text:trim(pattern, trim_left=yes, trim_right=no)

    func trim_right(text:Text, pattern:Pat -> Text):
        return text:trim(pattern, trim_left=no, trim_right=yes)

func main():
    >> "hello world":pat_replace($Pat/{id}/, "XXX")
    >> "hello world":find_all($Pat/l/)

    for m in "hello one two three":by_match($Pat/{id}/):
        >> m

