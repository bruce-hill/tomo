# Boolean Values

Boolean values have the type `Bool` and can be either `yes` ("true") or `no`
("false").

# Boolean Functions

This documentation provides details on boolean functions available in the API.

## `parse`

**Description:**  
Converts a string representation of a boolean value into a boolean. Acceptable
boolean values are case-insensitive variations of `yes`/`no`, `y`/`n`,
`true`/`false`, `on`/`off`.

**Signature:**  
```tomo
func parse(text: Text -> Bool?)
```

**Parameters:**

- `text`: The string containing the boolean value.

**Returns:**  
`yes` if the string matches a recognized truthy boolean value; otherwise return `no`.

**Example:**  
```tomo
>> Bool.parse("yes")
= yes?
>> Bool.parse("no")
= no?
>> Bool.parse("???")
= !Bool
```
