# Optional Values

A very common use case is values that may or may not be present. You could
represent this case using enums like so:

```tomo
enum MaybeInt(AnInt(x:Int), NoInt)

func maybe_takes_int(maybe_x:MaybeInt):
    when maybe_x is AnInt(x):
        say("Got an int: $x")
    else:
        say("Got nothing")
```

However, it's overly onerous to have to define a separate type for each
situation where you might want to not have a value. Instead, Tomo has
built-in support for optional types:

```
func maybe_takes_int(x:Int?):
    if x:
        say("Got an int: $x")
    else:
        say("Got nothing")
```

This establishes a common language for talking about optional values without
having to use a more generalized form of `enum` which may have different naming
conventions and which would generate a lot of unnecessary code.

## Syntax

Optional types are written using a `?` after the type name. So, an optional
integer would be written as `Int?` and an optional array of texts would be
written as `[Text]?`.

Null values can be written explicitly using the `!` prefix operator and the
type of null value. For example, if you wanted to declare a variable that could
be either an integer value or a null value and initialize it as a null value,
you would write it as:

```tomo
x := !Int
```

Similarly, if you wanted to declare a variable that could be an array of texts
or null and initialize it as null, you would write:

```tomo
x := ![Text]
```

If you want to declare a variable and initialize it with a non-null value, but
keep open the possibility of assigning a null value later, you can use the
postfix `?` operator to indicate that a value is optional:

```tomo
x := 5?
# Later on, assign null:
x = !Int
```

## Type Inference

For convenience, null values can also be written as `NONE` for any type in
situations where the compiler knows what type of optional value is expected:

- When assigning to a variable that has already been declared as optional.
- When returning from a function with an explicit optional return type.
- When passing an argument to a function with an optional argument type.

Here are some examples:

```tomo
x := 5?
x = NONE

func doop(arg:Int?)->Text?:
    return NONE

doop(NONE)
```

Non-null values can also be automatically promoted to optional values without
the need for an explicit `?` operator in the cases listed above:

```tomo
x := !Int
x = 5

func doop(arg:Int?)->Text?:
    return "okay"

doop(123)
```

## Null Checking

In addition to using conditionals to check for null values, you can also use
`or` to get a non-null value by either providing an alternative non-null value
or by providing an early out statement like `return`/`skip`/`stop` or a function
with an `Abort` type like `fail()` or `exit()`:

```tomo
maybe_x := 5?
>> maybe_x or -1
= 5 : Int
>> maybe_x or fail("No value!")
= 5 : Int

maybe_x = !Int
>> maybe_x or -1
= -1 : Int
>> maybe_x or fail("No value!")
# Failure!

func do_stuff(matches:[Text]):
    pass

for line in lines:
    matches := line:matches($/{..},{..}/) or skip
    # The `or skip` above means that if we're here, `matches` is non-null:
    do_stuff(matches)
```

## Implementation Notes

The implementation of optional types is highly efficient and has no memory
overhead for pointers, collection types (arrays, sets, tables, channels),
booleans, texts, enums, nums, or integers (`Int` type only). This is done by
using carefully chosen values, such as `0` for pointers, `2` for booleans, or a
negative length for arrays. However, for fixed-size integers (`Int64`, `Int32`,
`Int16`, and `Int8`), bytes, and structs, an additional byte is required for
out-of-band information about whether the value is null or not.

Floating point numbers (`Num` and `Num32`) use `NaN` to represent null, so
optional nums should be careful to avoid using `NaN` as a non-null value. This
option was chosen to minimize the memory overhead of optional nums and because
`NaN` literally means "not a number".
