# Byte Values

Byte values have the type `Byte`, which corresponds to an unsigned 8-bit
integer ranging from 0 to 255. It is generally recommended to use `Int8`
instead of `Byte` when performing math operations, however, `Byte`s are used in
API methods for `Text` and `Path` that deal with raw binary data, such as
`Path.read_bytes()` and `Text.utf8_bytes()`. Byte literals can be written as an
integer with a `[B]` suffix, e.g. `255[B]`.

# Byte Methods

## `random`

**Description:**  
Generates a random byte value in the specified range.

**Signature:**  
```tomo
func random(min: Byte = Byte.min, max: Byte = Byte.max -> Byte)
```

**Parameters:**

- `min`: The minimum value to generate (inclusive).
- `max`: The maximum value to generate (inclusive).

**Returns:**  
A random byte chosen with uniform probability from within the given range
(inclusive). If `min` is greater than `max`, an error will be raised.

**Example:**  
```tomo
>> Byte.random()
= 42[B]
```
