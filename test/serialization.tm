
struct Foo(name:Text, next:@Foo?=none)

enum MyEnum(Zero, One(x:Int), Two(x:Num, y:Text))

func main()
    do
        >> obj := Int64(123)
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Int64) == obj
        = yes

    do
        >> obj := 5
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Int) == obj
        = yes

    do
        >> obj := 9999999999999999999999999999999999999999999999999999
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Int) == obj
        = yes

    do
        >> obj := "Héllo"
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Text)
        >> deserialize(bytes -> Text) == obj
        = yes

    do
        >> obj := [Int64(10), Int64(20), Int64(30)].reversed()
        >> bytes := obj.serialized()
        >> deserialize(bytes -> [Int64]) == obj
        = yes

    do
        >> obj := yes
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Bool) == obj
        = yes

    do
        >> obj := @[10, 20]
        >> bytes := obj.serialized()
        >> roundtrip := deserialize(bytes -> @[Int])
        >> roundtrip == obj
        = no
        >> roundtrip[] == obj[]
        = yes

    do
        >> obj := {"A"=10, "B"=20; fallback={"C"=30}}
        >> bytes := obj.serialized()
        >> deserialize(bytes -> {Text=Int}) == obj
        = yes

    do
        >> obj := @Foo("root")
        >> obj.next = @Foo("abcdef", next=obj)
        >> bytes := obj.serialized()
        >> deserialize(bytes -> @Foo)
        # = @Foo(name="root", next=@Foo(name="abcdef", next=@~1))

    do
        >> obj := MyEnum.Two(123, "OKAY")
        >> bytes := obj.serialized()
        >> deserialize(bytes -> MyEnum) == obj
        = yes

    do
        >> obj := "Hello"?
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Text?) == obj
        = yes

    do
        >> obj := {10, 20, 30}
        >> bytes := obj.serialized()
        >> deserialize(bytes -> {Int}) == obj
        = yes

    do
        >> obj : Num? = none
        >> bytes := obj.serialized()
        >> deserialize(bytes -> Num?) == obj
        = yes

    do
        cases := [0, -1, 1, 10, 100000, 999999999999999999999999999]
        for i in cases
            >> bytes := i.serialized()
            >> deserialize(bytes -> Int) == i
            = yes
