# Serialization

Data serialization and deserialization is notoriously difficult to do correctly
and tedious to implement. In order to make this process easier, Tomo comes with
built-in support for serialization and deserialization of most built-in types,
as well as user-defined structs and enums. Serialization is a process that
takes Tomo values and converts them to bytes, which can be saved in a file or
sent over a network. Serialized bytes can the be deserialized to retrieve the
original value.

## Serializing

To serialize data, simply call the method `.serialized()` on any value and it
will return a list of bytes that encode the value's data:

```tomo
value := Int64(5)
>> serialized := value.serialized()
= [0x0A] : [Byte]
```

Serialization produces a fairly compact representation of data as a flat list
of bytes. In this case, a 64-bit integer can be represented in a single byte
because it's a small number.

## Deserializing 

To deserialize data, you must provide its type explicitly using the syntax
`deserialize(bytes -> Type)`:

```tomo
i := 123
bytes := i.serialized()

roundtripped := deserialize(bytes -> Int)
>> roundtripped
= 123 :Int
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
struct Cycle(name:Text, next:@Cycle?=none)

c := @Cycle("A")
c.next = @Cycle("B", next=c)
>> c
= @Cycle(name="A", next=@Cycle(name="B", next=@~1))
>> serialized := c.serialized()
= [0x02, 0x02, 0x41, 0x01, 0x04, 0x02, 0x42, 0x01, 0x02] : [Byte]
>> roundtrip := DESERIALIZE(serialized):@Cycle
= @Cycle(name="A", next=@Cycle(name="B", next=@~1)) : @Cycle
```

The deserialized version of the data correctly preserves the cycle
(`roundtrip.next.next == roundtrip`). The representation is also very compact:
only 9 bytes for the whole thing!

## Unserializable Types

Unfortunately, not all types can be easily serialized. In particular, types and
functions cannot be serialized because their data contents cannot be easily
converted to portable byte lists. All other datatypes _can_ be serialized.
