
struct Foo{name:Text, next:@Foo?=none}

struct Flag{flag:Bool}

enum MyEnum(Zero, One{x:Int}, Two{x:Float64, y:Text})

test "Int64 roundtrip"
    >> obj := Int64(123)
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Int64(bytes)!
    assert roundtrip == obj

test "Int16 roundtrip"
    >> obj := Int16(-12345)
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Int16(bytes)!
    assert roundtrip == obj

test "Int roundtrip"
    >> obj := 5
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Int(bytes)!
    assert roundtrip == obj

test "big Int roundtrip"
    >> obj := 9999999999999999999999999999999999999999999999999999
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Int(bytes)!
    assert roundtrip == obj

test "Text roundtrip"
    >> obj := "Héllo"
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Text(bytes)!
    assert roundtrip == obj

test "list roundtrip"
    >> obj := [Int64(10), Int64(20), Int64(30)].reversed()
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:[Int64](bytes)!
    assert roundtrip == obj

test "Bool roundtrip"
    >> obj := yes
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Bool(bytes)!
    assert roundtrip == obj

test "heap list roundtrip"
    >> obj := @[10, 20]
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:@[Int](bytes)!
    assert roundtrip != obj
    >> roundtrip[]
    assert roundtrip[] == obj[]

test "table with fallback roundtrip"
    >> obj := {"A":10, "B":20; fallback={"C":30}}
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:{Text:Int}(bytes)!
    assert roundtrip == obj
    >> roundtrip.fallback
    assert roundtrip.fallback == obj.fallback

test "cyclic struct roundtrip"
    >> obj := @Foo{"root"}
    >> obj.next = @Foo{"abcdef", next=obj}
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:@Foo(bytes)!
    >> "$roundtrip"
    assert "$roundtrip" == "$obj"

test "enum roundtrip"
    >> obj := MyEnum.Two{123, "OKAY"}
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:MyEnum(bytes)!
    assert roundtrip == obj

test "optional Text roundtrip"
    >> obj : Text? = "Hello"
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Text?(bytes)
    assert roundtrip == obj

# When the target type is itself optional, `none` does double duty: it means
# either "these bytes didn't decode" or "these bytes encoded a `none`".
test "optional none roundtrip"
    >> obj : Float64? = none
    >> bytes := serialize(obj)
    >> roundtrip := deserialize:Float64?(bytes)
    assert roundtrip == obj

test "corrupt data deserializes to none"
    >> bytes := serialize("Hello")
    >> deserialize:Text(bytes.to(2))
    assert deserialize:Text(bytes.to(2)) == none

    # Trailing garbage isn't a well-formed encoding either:
    >> deserialize:Text(bytes ++ [Byte(0xFF)])
    assert deserialize:Text(bytes ++ [Byte(0xFF)]) == none

    >> deserialize:[Int](serialize("Hello"))
    assert deserialize:[Int](serialize("Hello")) == none

test "invalid text bytes deserialize to none"
    # Not every byte sequence is valid text:
    >> deserialize:Text([Byte(0x04), Byte(0xFF), Byte(0xFF), Byte(0xFF), Byte(0xFF)])
    assert deserialize:Text([Byte(0x04), Byte(0xFF), Byte(0xFF), Byte(0xFF), Byte(0xFF)]) == none

test "no nested optionals"
    >> bytes := serialize(5)
    # `deserialize:Int(...)` is an `Int?`, so `or` works as usual:
    >> deserialize:Int(bytes) or -1
    assert (deserialize:Int(bytes) or -1) == 5
    >> deserialize:Int([Byte(0xFF)]) or -1
    assert (deserialize:Int([Byte(0xFF)]) or -1) == -1

test "Int cases roundtrip"
    >> cases := [0, -1, 1, 10, 100000, 999999999999999999999999999]
    for i in cases
        >> i
        >> bytes := serialize(i)
        >> roundtrip := deserialize:Int(bytes)!
        assert roundtrip == i

# Optionals of types that carry an explicit `has_value` flag after the value
# (structs, fixed-width ints, bytes) have to get that flag set on the way back
# in, or every roundtrip answers `none`:
test "optional struct roundtrip"
    >> obj : Foo? = Foo{"Alice"}
    >> roundtrip := deserialize:Foo?(serialize(obj))!
    assert roundtrip == obj

test "optional fixed-width int roundtrip"
    >> obj : Int64? = 123
    >> roundtrip := deserialize:Int64?(serialize(obj))!
    assert roundtrip == obj

test "optional Byte roundtrip"
    >> obj : Byte? = Byte(3)
    >> roundtrip := deserialize:Byte?(serialize(obj))!
    assert roundtrip == obj

test "optional Bool roundtrip"
    some_bool : Bool? = no
    >> deserialize:Bool?(serialize(some_bool))
    assert deserialize:Bool?(serialize(some_bool)) == no
    # `Bool??` collapses to `Bool?`, so a serialized `none` comes back as the
    # same `none` a failed decode gives, but never as `yes`/`no`:
    none_bool : Bool? = none
    >> deserialize:Bool?(serialize(none_bool))
    assert deserialize:Bool?(serialize(none_bool)) == none

test "lists of optionals roundtrip"
    >> obj : [Int64?] = [Int64(1), none, Int64(3)]
    >> roundtrip := deserialize:[Int64?](serialize(obj))!
    assert roundtrip == obj

# A `Byte?` is a byte plus a `has_value` flag, so it's wider than a `Byte`; if
# the compiler sizes it as one byte, the flags land on top of the next element:
test "list of optional Bytes roundtrip"
    >> obj : [Byte?] = [Byte(5), none, Byte(7)]
    >> roundtrip := deserialize:[Byte?](serialize(obj))!
    assert roundtrip == obj

# Struct Bools are bit-packed, and the table deserializer decodes every entry
# into one reused buffer, so a `no` after a `yes` must not inherit the set bit:
test "table keyed by a Bool-carrying struct roundtrips every entry"
    >> obj : {Flag:Int} = {Flag{yes}: 1, Flag{no}: 2}
    >> roundtrip := deserialize:{Flag:Int}(serialize(obj))!
    assert roundtrip == obj
