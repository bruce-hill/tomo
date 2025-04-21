% API

# Builtins

# Bool
## Bool.parse

```tomo
Bool.parse : func(text: Text -> Bool?)
```

Converts a text representation of a boolean value into a boolean. Acceptable boolean values are case-insensitive variations of `yes`/`no`, `y`/`n`, `true`/`false`, `on`/`off`.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The string containing the boolean value.  | -

**Return:** `yes` if the string matches a recognized truthy boolean value; otherwise return `no`.


**Example:**
```tomo
>> Bool.parse("yes")
= yes : Bool?
>> Bool.parse("no")
= no : Bool?
>> Bool.parse("???")
= none : Bool?

```
