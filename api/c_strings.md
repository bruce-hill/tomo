% API

# Builtins

# CString
## CString.as_text

```tomo
CString.as_text : func(str: CString -> Text)
```

Convert a C string to Text.

Argument | Type | Description | Default
---------|------|-------------|---------
str | `CString` | The C string.  | -

**Return:** The C string as a Text.


**Example:**
```tomo
assert CString("Hello").as_text() == "Hello"

```
## CString.join

```tomo
CString.join : func(glue: CString, pieces: [CString] -> CString)
```

Join a list of C strings together with a separator.

Argument | Type | Description | Default
---------|------|-------------|---------
glue | `CString` | The C joiner used to between elements.  | -
pieces | `[CString]` | A list of C strings to join.  | -

**Return:** A C string of the joined together bits.


**Example:**
```tomo
assert CString(",").join([CString("a"), CString("b")]) == CString("a,b")

```
