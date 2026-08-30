func main()
    name := "world"
    >> "hello $name"
    >> "sum: $(1 + 2)"
    # $name takes the bare identifier, so ".length" stays literal text; an
    # expression needs the parenthesized form:
    >> "$name.length"
    >> "$(name.length)"
    >> "escaped: \$name"
    >> "
        multiline
        text
    "
