
struct Foo(name:Text, next:@Foo?=none)

enum MyEnum(Zero, One(x:Int), Two(x:Num, y:Text))

func main()
    do
        >> obj := Int64(123)
        >> bytes : [Byte] = obj
        >> roundtrip : Int64 = bytes
        assert roundtrip == obj

    do
        >> obj := 5
        >> bytes : [Byte] = obj
        >> roundtrip : Int = bytes
        assert roundtrip == obj

    do
        >> obj := 9999999999999999999999999999999999999999999999999999
        >> bytes : [Byte] = obj
        >> roundtrip : Int = bytes
        assert roundtrip == obj

    do
        >> obj := "Héllo"
        >> bytes : [Byte] = obj
        >> roundtrip : Text = bytes
        assert roundtrip == obj

    do
        >> obj := [Int64(10), Int64(20), Int64(30)].reversed()
        >> bytes : [Byte] = obj
        >> roundtrip : [Int64] = bytes
        assert roundtrip == obj

    do
        >> obj := yes
        >> bytes : [Byte] = obj
        >> roundtrip : Bool = bytes
        assert roundtrip == obj

    do
        >> obj := @[10, 20]
        >> bytes : [Byte] = obj
        >> roundtrip : @[Int] = bytes
        assert roundtrip != obj
        assert roundtrip[] == obj[]

    do
        >> obj := {"A":10, "B":20; fallback={"C":30}}
        >> bytes : [Byte] = obj
        >> roundtrip : {Text:Int} = bytes
        assert roundtrip == obj
        assert roundtrip.fallback == obj.fallback

    do
        >> obj := @Foo("root")
        >> obj.next = @Foo("abcdef", next=obj)
        >> bytes : [Byte] = obj
        >> roundtrip : @Foo = bytes
        assert "$roundtrip" == "$obj"

    do
        >> obj := MyEnum.Two(123, "OKAY")
        >> bytes : [Byte] = obj
        >> roundtrip : MyEnum = bytes
        assert roundtrip == obj

    do
        >> obj : Text? = "Hello"
        >> bytes : [Byte] = obj
        >> roundtrip : Text? = bytes
        assert roundtrip == obj

    do
        >> obj : Num? = none
        >> bytes : [Byte] = obj
        >> roundtrip : Num? = bytes
        assert roundtrip == obj

    do
        cases := [0, -1, 1, 10, 100000, 999999999999999999999999999]
        for i in cases
            >> bytes : [Byte] = i
            >> roundtrip : Int = bytes
            assert roundtrip == i
