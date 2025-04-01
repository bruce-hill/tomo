use ./patterns.c

struct PatternMatch(text:Text, index:Int, captures:[Text])

lang P:
    convert(text:Text -> P):
        return inline C : P { Pattern$escape_text(_$text); }

    convert(n:Int -> P):
        return P.from_text("$n")

extend Text:
    func matches(text:Text, pattern:P -> [Text]?):
        return inline C : [Text]? { Pattern$matches(_$text, _$pattern); }

    func pat_replace(text:Text, pattern:P, replacement:Text, backref="@", recursive=yes -> Text):
        return inline C : Text { Pattern$replace(_$text, _$pattern, _$replacement, _$backref, _$recursive); }

    func pat_replace_all(text:Text, replacements:{P,Text}, backref="@", recursive=yes -> Text):
        return inline C : Text { Pattern$replace_all(_$text, _$replacements, _$backref, _$recursive); }

    func has(text:Text, pattern:P -> Bool):
        return inline C : Bool { Pattern$has(_$text, _$pattern); }

    func find_all(text:Text, pattern:P -> [PatternMatch]):
        return inline C : [PatternMatch] { Pattern$find_all(_$text, _$pattern); }

    func by_match(text:Text, pattern:P -> func(->PatternMatch?)):
        return inline C : func(->PatternMatch?) { Pattern$by_match(_$text, _$pattern); }

    func each(text:Text, pattern:P, fn:func(m:PatternMatch), recursive=yes):
        inline C { Pattern$each(_$text, _$pattern, _$fn, _$recursive); }

    func map(text:Text, pattern:P, fn:func(m:PatternMatch -> Text), recursive=yes -> Text):
        return inline C : Text { Pattern$map(_$text, _$pattern, _$fn, _$recursive); }

    func split(text:Text, pattern:P -> [Text]):
        return inline C : [Text] { Pattern$split(_$text, _$pattern); }

    func by_split(text:Text, pattern:P -> func(->Text?)):
        return inline C : func(->Text?) { Pattern$by_split(_$text, _$pattern); }

    func trim(text:Text, pattern:P, trim_left=yes, trim_right=yes -> Text):
        return inline C : Text { Pattern$trim(_$text, _$pattern, _$trim_left, _$trim_right); }

    func trim_left(text:Text, pattern:P -> Text):
        return text:trim(pattern, trim_left=yes, trim_right=no)

    func trim_right(text:Text, pattern:P -> Text):
        return text:trim(pattern, trim_left=no, trim_right=yes)

func main():
    >> "hello world":pat_replace($P/{id}/, "XXX")
    >> "hello world":find_all($P/l/)

    for m in "hello one two three":by_match($P/{id}/):
        >> m

