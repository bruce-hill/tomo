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

    func get_older(f:@Foo):
        f.age += 1
...
my_foo := @Foo("Alice", 28)
my_foo.greet()
my_foo.get_older()
```

Method calls work when the first argument is the struct type or a pointer to
the struct type.

## Secret Values

If you want to prevent accidental leaking of sensitive information, you can
create a struct with the `secret` flag turned on, which causes the struct to
be converted to text without showing any of its contents:

```tomo
struct Password(raw_password_text:Text; secret)
struct User(username:Text, password:Password)
...
user := User("Stanley", Password("Swordfish"))
>> user
= User(username="Stanley", password=Password(...))

>> "$user" == 'User(username="Stanley", password=Password(...))'
= yes
```

Designing APIs so they take secrecy-protected structs instead of raw data
values is a great way to prevent accidentally leaking sensitive information in
your logs! Secrecy-protected values still work the same as any other struct,
they just don't divulge their contents when converting to strings:

```tomo
>> user.password == Password("Swordfish")
= yes
```

You can also access the fields directly, but hopefully this extra amount of
friction reduces the chances of accidentally divulging sensitive content:

```tomo
>> user.password
= Password(...)

>> user.password.raw_password_text
= "Swordfish"
```
