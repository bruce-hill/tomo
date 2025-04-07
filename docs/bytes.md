# Byte Values

Byte values have the type `Byte`, which corresponds to an unsigned 8-bit
integer ranging from 0 to 255. It is generally recommended to use `Int8`
instead of `Byte` when performing math operations, however, `Byte`s are used in
API methods for `Text` and `Path` that deal with raw binary data, such as
`Path.read_bytes()` and `Text.bytes()`. Byte literals can be written using
the `Byte()` constructor: `Byte(5)`.

# Byte Methods

- [`func hex(byte: Byte, uppercase=no, prefix=yes -> Text)`](#hex)
- [`func is_between(x: Byte, low: Byte, high: Byte -> Bool)`](#is_between)
- [`func parse(text: Text -> Byte?)`](#parse)
- [`func to(first: Byte, last: Byte, step: Int8? = none -> Text)`](#to)

---------

## `hex`

TODO: write docs

---------

### `is_between`
Determines if an integer is between two numbers (inclusive).

```tomo
func is_between(x: Byte, low: Byte, high: Byte -> Bool)
```

- `x`: The integer to be checked.
- `low`: The lower bound to check (inclusive).
- `high`: The upper bound to check (inclusive).

**Returns:**  
`yes` if `low <= x and x <= high`, otherwise `no`

**Example:**  
```tomo
>> Byte(7).is_between(1, 10)
= yes
>> Byte(7).is_between(100, 200)
= no
>> Byte(7).is_between(1, 7)
= yes
```

---

## `parse`

TODO: write docs

---------

## `to`

TODO: write docs
