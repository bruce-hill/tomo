% API

# Builtins

# Bool
## Bool.parse

```tomo
Bool.parse : func(text: Text, remainder: &Text? = none -> Bool?)
```

Converts a text representation of a boolean value into a boolean. Acceptable boolean values are case-insensitive variations of `yes`/`no`, `y`/`n`, `true`/`false`, `on`/`off`.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The string containing the boolean value.  | -
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** `yes` if the string matches a recognized truthy boolean value; otherwise return `no`.


**Example:**
```tomo
>> Bool.parse("yes")
= yes : Bool?
>> Bool.parse("no")
= no : Bool?
>> Bool.parse("???")
= none : Bool?

>> Bool.parse("yesJUNK")
= none : Bool?
remainder : Text
>> Bool.parse("yesJUNK", &remainder)
= yes : Bool?
>> remainder
= "JUNK"

```
