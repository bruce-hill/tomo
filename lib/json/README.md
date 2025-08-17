# JSON

This is a library for encoding/decoding JSON values.

## Usage

```tomo
>> j := JSON({"key1"=123, "key2"=[yes, {"ok"="inner"}, JSON.Null]})
= JSON.Object({"key1"=Number(123), "key2"=Array([Boolean(yes), Object({"ok"=String("inner")}), Null])})

say("$(j.encode())")
say("$(j.pretty_print())")

when JSON.parse("[1, null, true]") is Success(obj)
    >> obj
is Failure(msg)
    fail("Failed to parse JSON: $msg")
```
