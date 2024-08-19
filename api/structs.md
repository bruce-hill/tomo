# Structs

In Tomo, you can define your own structs, which hold members with arbitrary
types that can be accessed by fields:

```tomo
struct Foo(name:Text, age:Int)
...
>> my_foo := Foo("Bob", age=10)
= Foo(name="Bob", age=10)
>> my_foo.name
= "Bob"
```

Structs are value types and comparisons on them operate on the member values
one after the other.

## Namespaces

Structs can define their own methods that can be called with a `:` or different
values that are stored on the type itself.

```tomo
struct Foo(name:Text, age:Int):
    oldest := Foo("Methuselah", 969)

    func greet(f:Foo):
        say("Hi my name is $f.name and I am $f.age years old!")

    func get_older(f:&Foo):
        f.age += 1
...
my_foo := Foo("Alice", 28)
my_foo:greet()
my_foo:get_older()
```

Method calls work when the first argument is the struct type or a pointer to
the struct type.
