# Structs

In Tomo, you can define your own structs, which hold members with arbitrary
types that can be accessed by fields:

```tomo
struct Foo{name:Text, age:Int}
...
my_foo := Foo{"Bob", age=10}
assert my_foo == Foo{name="Bob", age=10}
assert my_foo.name == "Bob"
```

Structs are value types and comparisons on them operate on the member values
one after the other.

## Namespaces

Structs can define their own methods that can be called with a `:` or different
values that are stored on the type itself.

```tomo
struct Foo{name:Text, age:Int}
    oldest := Foo{"Methuselah", 969}

    func greet(f:Foo)
        say("Hi my name is $(f.name) and I am $(f.age) years old!")

    func get_older(f:@Foo)
        f.age += 1
...
my_foo := @Foo{"Alice", 28}
my_foo.greet()
my_foo.get_older()
```

Method calls work when the first argument is the struct type or a pointer to
the struct type.

## Packed Booleans

Every field of a struct normally gets its own byte or more, so a struct of eight
`Bool` fields takes eight bytes. Adding the `packed_bools` flag stores each
`Bool` field in a single bit and each `Bool?` field in two (`yes`, `no`, and
`none` all fit), so adjacent boolean fields share bytes:

```tomo
struct Flags{a:Bool, b:Bool, c:Bool, d:Bool, e:Bool, f:Bool, g:Bool, h:Bool; packed_bools}
...
# One byte instead of eight:
flags := Flags{yes, no, yes, no, yes, no, yes, no}
assert flags.c
```

Packed booleans behave exactly like unpacked ones -- they compare, hash, sort,
print, and serialize the same way -- with one exception: a bit-packed field has
no address of its own, so you can't point at one.

```tomo
struct Flags{a:Bool, b:Bool; packed_bools}
...
flags := @Flags{yes, no}
flags.a = no        # Assignment is fine
p := &flags.a       # Compile error: no address to point at
p := &flags         # Point at the whole struct instead
```

Only `Bool` and `Bool?` fields are affected. Every other field keeps its natural
size and alignment, so mixing them in costs nothing:

```tomo
struct Entry{name:Text, active:Bool, hidden:Bool, count:Int32; packed_bools}
```

The flag only pays off for *adjacent* boolean fields, since a lone `Bool`
between two larger fields usually fits in padding that would otherwise go
unused. That makes the saving depend on field order: in the struct above,
`active` and `hidden` share a byte, but they wouldn't if `count` sat between
them. Structs whose layout comes from C (`external`) can't be packed.

## Secret Values

If you want to prevent accidental leaking of sensitive information, you can
create a struct with the `secret` flag turned on, which causes the struct to
be converted to text without showing any of its contents:

```tomo
struct Password{raw_password_text:Text; secret}
struct User{username:Text, password:Password}
...
user := User{"Stanley", Password{"Swordfish"}}
assert user == User{"Stanley", Password{"Swordfish"}}
assert "You are: $user" == 'You are: User{username="Stanley", password=Password{...}}'
```

Designing APIs so they take secrecy-protected structs instead of raw data
values is a great way to prevent accidentally leaking sensitive information in
your logs! Secrecy-protected values still work the same as any other struct,
they just don't divulge their contents when converting to strings:

```tomo
assert user.password == Password{"Swordfish"}
```

You can also access the fields directly, but hopefully this extra amount of
friction reduces the chances of accidentally divulging sensitive content:

```tomo
assert user.password.raw_password_text == "Swordfish"
```
