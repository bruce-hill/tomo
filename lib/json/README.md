# JSON

This is a library for encoding/decoding JSON values.

## Usage

```tomo
j := JSON.Object({"key": Number(1.5), "key": Array([Boolean(yes), Null])})
say("$(j.encode())")
say("$(j.pretty_print())")

when JSON.parse("[1, null, true]") is Success(obj)
    >> obj
is Failure(msg)
    fail("Failed to parse JSON: $msg")
```
