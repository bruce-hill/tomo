% API

# Builtins

# CString
## CString.as_text

```tomo
CString.as_text : func(str: CString -> Text?)
```

Convert a C string to Text. This is the same conversion that `Text(str)` performs.

A C string is an arbitrary sequence of non-NUL bytes, but `Text` holds Unicode, so this returns `none` when the bytes are not valid UTF-8.

Argument | Type | Description | Default
---------|------|-------------|---------
str | `CString` | The C string.  | -

**Return:** The C string as a Text, or `none` if it is not valid UTF-8.


**Example:**
```tomo
assert CString("Hello").as_text() == "Hello"
assert Text(CString("Hello"))! == "Hello"

```
## CString.bytes

```tomo
CString.bytes : func(str: CString -> [Byte])
```

Convert a C string to a list of its raw bytes.

Argument | Type | Description | Default
---------|------|-------------|---------
str | `CString` | The C string.  | -

**Return:** A list of bytes (`[Byte]`) representing the C string's contents.


**Example:**
```tomo
assert CString("Hi").bytes() == [72, 105]

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
