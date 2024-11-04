# Byte Values

Byte values have the type `Byte`, which corresponds to an unsigned 8-bit
integer ranging from 0 to 255. It is generally recommended to use `Int8`
instead of `Byte` when performing math operations, however, `Byte`s are used in
API methods for `Text` and `Path` that deal with raw binary data, such as
`Path.read_bytes()` and `Text.utf8_bytes()`. Byte literals can be written as an
integer with a `[B]` suffix, e.g. `255[B]`.

# Byte Methods

None.
