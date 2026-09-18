# Byte Values

Byte values have the type `Byte`, which corresponds to an unsigned 8-bit
integer ranging from 0 to 255. It is generally recommended to use `Int8`
instead of `Byte` when performing math operations, however, `Byte`s are used in
API methods for `Text` and `Path` that deal with raw binary data, such as
`Path.read_bytes()` and `Text.utf8()`. Byte literals can be written using
the `Byte()` constructor: `Byte(5)`.

Bytes support the bitwise operations `x and y`, `x or y`, `x xor y`, and `not
x` (bitwise negation, flipping all eight bits), as well as bit shifting: `x <<
y`, `x >> y`, `x <<< y`, and `x >>> y`.

# API

[API documentation](../api/bytes.md)
