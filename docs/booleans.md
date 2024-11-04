# Boolean Values

Boolean values have the type `Bool` and can be either `yes` ("true") or `no`
("false").

# Boolean Functions

This documentation provides details on boolean functions available in the API.

## `from_text`

**Description:**  
Converts a string representation of a boolean value into a boolean. Acceptable
boolean values are case-insensitive variations of `yes`/`no`, `y`/`n`,
`true`/`false`, `on`/`off`.

**Signature:**  
```tomo
func from_text(text: Text -> Bool?)
```

**Parameters:**

- `text`: The string containing the boolean value.

**Returns:**  
`yes` if the string matches a recognized truthy boolean value; otherwise return `no`.

**Example:**  
```tomo
>> Bool.from_text("yes")
= yes?
>> Bool.from_text("no")
= no?
>> Bool.from_text("???")
= !Bool
```
