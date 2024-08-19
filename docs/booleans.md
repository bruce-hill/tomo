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

**Usage:**  
```tomo
from_text(text: Text, success: Bool = !&Bool) -> Bool
```

**Parameters:**

- `text`: The string containing the boolean value.
- `success`: If provided, this boolean value reference will be set to `yes` if the given text is a recognizable boolean value or `no` otherwise.

**Returns:**  
`yes` if the string matches a recognized truthy boolean value; otherwise return `no`.

**Example:**  
```tomo
>> Bool.from_text("yes")
= yes
>> Bool.from_text("no")
= no
>> success := yes
>> Bool.from_text("???", &success)
= no
>> success
= no
```

---

## `random`

**Description:**  
Generates a random boolean value based on a specified probability.

**Usage:**  
```tomo
random(p: Float = 0.5) -> Bool
```

**Parameters:**

- `p`: The probability (between `0` and `1`) of returning `yes`. Default is `0.5`.

**Returns:**  
`yes` with probability `p`, and `no` with probability `1 - p`.

**Example:**  
```tomo
>> Bool.random(70%)  // yes (with 70% probability)
```
