# Serialization

Data serialization and deserialization is notoriously difficult to do correctly
and tedious to implement. In order to make this process easier, Tomo comes with
built-in support for serialization and deserialization of most built-in types,
as well as user-defined structs and enums. Serialization is a process that
takes Tomo values and converts them to bytes, which can be saved in a file or
sent over a network. Serialized bytes can then be deserialized to retrieve the
original value.

## Serializing

To serialize data, use `serialize(...)`, which takes a value of any serializable
type and gives back a `[Byte]`:

```tomo
value := Int64(5)
serialized := serialize(value)
assert serialized == [0x0A]
```

Serialization produces a fairly compact representation of data as a flat list
of bytes. In this case, a 64-bit integer can be represented in a single byte
because it's a small number.

The same process works with more complicated data:

```tomo
struct Foo{x:Int, y:Text}

foo := Foo{123, "Hello"}
serialized := serialize(foo)
assert serialized == [0x00, 0xf6, 0x01, 0x0a, 0x48, 0x65, 0x6c, 0x6c, 0x6f]
```

## Deserializing

To deserialize data, use `deserialize:Type(bytes)`, which takes a `[Byte]` list
and gives back an optional value of the requested type:

```tomo
value_bytes := [Byte(0x0A)]
value := deserialize:Int64(value_bytes)
assert value == 5

foo_bytes : [Byte] = [0x00, 0xf6, 0x01, 0x0a, 0x48, 0x65, 0x6c, 0x6c, 0x6f]
foo := deserialize:Foo(foo_bytes)
assert foo == Foo{123, "Hello"}
```

`serialize` and `deserialize` are soft keywords, so both are still usable as
ordinary identifiers (variable names, struct fields, and so on). The one
exception is a *function* named `serialize`: `serialize(...)` always means the
construct, so such a function would be unreachable, and defining one is a
compile error.

Note that `deserialize:Foo(...)` is a language construct, not a function call:
the type comes first, between the colon and the parentheses, because the type
of the result depends on it.

Since the result is optional, all the usual [optional](optionals.md) tools
apply:

```tomo
# Give up with a runtime error if the data is bad:
foo := deserialize:Foo(bytes)!

# Or fall back to a default:
foo := deserialize:Foo(bytes) or Foo{0, ""}

# Or handle it explicitly:
if foo := deserialize:Foo(bytes)
    say("Got $foo")
else
    say("That wasn't a Foo!")
```

## Failure

Deserialization returns `none` when the bytes aren't a well-formed encoding of
the requested type: truncated data, a length or enum tag that doesn't make
sense, or trailing bytes left over at the end.

However, the encoding is compact and doesn't record which type it came from, so
a well-formed encoding of one type can also be a well-formed encoding of
another. Deserializing a `Foo` from bytes that were serialized from a `Baz` may
well succeed and give you a nonsensical `Foo` rather than `none`.
Deserialization is safe against corrupt or hostile input in the sense that it
won't read out of bounds or allocate wildly, but it can't tell you that the
bytes *meant* something else. If you need that guarantee, include a version or
type marker in the data yourself.

## Optional Types

Tomo doesn't have nested optionals, so when the type you're deserializing is
itself optional, the result is a single optional and `none` does double duty:

```tomo
maybe_text : Text? = none
bytes := serialize(maybe_text)
# This is a `Text?`, not a `Text??`:
value := deserialize:Text?(bytes)
```

Here, `value` is `none` if the bytes failed to decode *or* if they successfully
decoded a `none`. If you need to tell those two cases apart, you can use an
`enum` like `enum MaybeText(NoText, SomeText(text:Text))`:

```tomo
maybe_text := MaybeText.NoText
bytes := serialize(maybe_text)
value := deserialize:MaybeText(bytes)
if value
    match value
    case NoText ...
    case SomeText(text) ...
```

## Pointers

In the case of pointers, deserialization creates a new heap-allocated region of
memory for the values. This means that if you serialize a pointer, it will
store all of the memory contents of that pointer, but not the literal memory
address of the pointer, which may not be valid memory when deserialization
occurs. The upshot is that you can easily serialize datastructures that rely on
pointers, but pointers returned from deserialization will point to new memory
and will not point to the same memory as any pre-existing pointers.

One of the nice things about this process is that it automatically handles
cyclic datastructures correctly, enabling you to serialize cyclic structures
like circularly linked lists or graphs:

```tomo
struct Cycle{name:Text, next:@Cycle?=none}

c := @Cycle{"A"}
c.next = @Cycle{"B", next=c}
say("$c")
# @Cycle{name="A", next=@Cycle{name="B", next=@~1}}
bytes := serialize(c)
say("$bytes")
# [0x02, 0x02, 0x41, 0x01, 0x04, 0x02, 0x42, 0x01, 0x02]
roundtrip := deserialize:@Cycle(bytes)!
say("$roundtrip")
# @Cycle{name="A", next=@Cycle{name="B", next=@~1}}
assert roundtrip.next!.next! == roundtrip
```

The deserialized version of the data correctly preserves the cycle
(`roundtrip.next!.next! == roundtrip`). The representation is also very compact:
only 9 bytes for the whole thing!

## Unserializable Types

Unfortunately, not all types can be easily serialized. In particular, functions
(and closures) cannot be serialized because their data contents cannot be
easily converted to portable byte lists. Type objects themselves (e.g. the
variable `Text`) also cannot be serialized. All other datatypes _can_ be
serialized. Attempting to serialize or deserialize a type that contains one of
these is a compile-time error, not a runtime failure.
