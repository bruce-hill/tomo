
struct Foo(name:Text, next:@Foo?=none)

enum MyEnum(Zero, One(x:Int), Two(x:Num, y:Text))

func main()
    do
        >> obj := Int64(123)
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Int64) == obj

    do
        >> obj := 5
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Int) == obj

    do
        >> obj := 9999999999999999999999999999999999999999999999999999
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Int) == obj

    do
        >> obj := "Héllo"
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Text) == obj

    do
        >> obj := [Int64(10), Int64(20), Int64(30)].reversed()
        >> bytes := obj.serialized()
        assert deserialize(bytes -> [Int64]) == obj

    do
        >> obj := yes
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Bool) == obj

    do
        >> obj := @[10, 20]
        >> bytes := obj.serialized()
        >> roundtrip := deserialize(bytes -> @[Int])
        assert roundtrip != obj
        assert roundtrip[] == obj[]

    do
        >> obj := {"A"=10, "B"=20; fallback={"C"=30}}
        >> bytes := obj.serialized()
        >> roundtrip := deserialize(bytes -> {Text=Int})
        assert roundtrip == obj
        assert roundtrip.fallback == obj.fallback

    do
        >> obj := @Foo("root")
        >> obj.next = @Foo("abcdef", next=obj)
        >> bytes := obj.serialized()
        >> roundtrip := deserialize(bytes -> @Foo)
        assert "$roundtrip" == "$obj"

    do
        >> obj := MyEnum.Two(123, "OKAY")
        >> bytes := obj.serialized()
        assert deserialize(bytes -> MyEnum) == obj

    do
        >> obj : Text? = "Hello"
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Text?) == obj

    do
        >> obj : Num? = none
        >> bytes := obj.serialized()
        assert deserialize(bytes -> Num?) == obj

    do
        cases := [0, -1, 1, 10, 100000, 999999999999999999999999999]
        for i in cases
            >> bytes := i.serialized()
            assert deserialize(bytes -> Int) == i
