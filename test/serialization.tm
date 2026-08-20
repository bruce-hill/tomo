
struct Foo{name:Text, next:@Foo?=none}

enum MyEnum(Zero, One{x:Int}, Two{x:Num, y:Text})

test "Int64 roundtrip"
    >> obj := Int64(123)
    >> bytes : [Byte] = obj
    >> roundtrip : Int64 = bytes
    assert roundtrip == obj

test "Int roundtrip"
    >> obj := 5
    >> bytes : [Byte] = obj
    >> roundtrip : Int = bytes
    assert roundtrip == obj

test "big Int roundtrip"
    >> obj := 9999999999999999999999999999999999999999999999999999
    >> bytes : [Byte] = obj
    >> roundtrip : Int = bytes
    assert roundtrip == obj

test "Text roundtrip"
    >> obj := "Héllo"
    >> bytes : [Byte] = obj
    >> roundtrip : Text = bytes
    assert roundtrip == obj

test "list roundtrip"
    >> obj := [Int64(10), Int64(20), Int64(30)].reversed()
    >> bytes : [Byte] = obj
    >> roundtrip : [Int64] = bytes
    assert roundtrip == obj

test "Bool roundtrip"
    >> obj := yes
    >> bytes : [Byte] = obj
    >> roundtrip : Bool = bytes
    assert roundtrip == obj

test "heap list roundtrip"
    >> obj := @[10, 20]
    >> bytes : [Byte] = obj
    >> roundtrip : @[Int] = bytes
    assert roundtrip != obj
    >> roundtrip[]
    assert roundtrip[] == obj[]

test "table with fallback roundtrip"
    >> obj := {"A":10, "B":20; fallback={"C":30}}
    >> bytes : [Byte] = obj
    >> roundtrip : {Text:Int} = bytes
    assert roundtrip == obj
    >> roundtrip.fallback
    assert roundtrip.fallback == obj.fallback

test "cyclic struct roundtrip"
    >> obj := @Foo{"root"}
    >> obj.next = @Foo{"abcdef", next=obj}
    >> bytes : [Byte] = obj
    >> roundtrip : @Foo = bytes
    >> "$roundtrip"
    assert "$roundtrip" == "$obj"

test "enum roundtrip"
    >> obj := MyEnum.Two{123, "OKAY"}
    >> bytes : [Byte] = obj
    >> roundtrip : MyEnum = bytes
    assert roundtrip == obj

test "optional Text roundtrip"
    >> obj : Text? = "Hello"
    >> bytes : [Byte] = obj
    >> roundtrip : Text? = bytes
    assert roundtrip == obj

test "optional none roundtrip"
    >> obj : Num? = none
    >> bytes : [Byte] = obj
    >> roundtrip : Num? = bytes
    assert roundtrip == obj

test "Int cases roundtrip"
    >> cases := [0, -1, 1, 10, 100000, 999999999999999999999999999]
    for i in cases
        >> i
        >> bytes : [Byte] = i
        >> roundtrip : Int = bytes
        assert roundtrip == i
