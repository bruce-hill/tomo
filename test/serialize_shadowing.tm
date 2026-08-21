# `serialize(...)` always parses as the built-in construct, so a function by
# that name would be silently unreachable. The compiler rejects the ambiguity
# instead of quietly ignoring the function.

func serialize(x:Int -> Text)
    return "custom"

test "a `serialize` function is rejected rather than silently shadowed"
    _ := serialize(5)
fails_compile "shadows the `serialize` function"
