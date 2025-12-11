# Enums

Tomo supports tagged enumerations, also known as "sum types." Users
can define their own using the `enum` keyword:

```tomo
enum VariousThings(AnInteger(i:Int), TwoWords(word1, word2:Text), Nothing)

...

a := VariousThings.AnInteger(5)
b := VariousThings.TwoWords("one", "two")
c := VariousThings.Nothing
```

## Pattern Matching

The values inside an enum can be accessed with pattern matching

```tomo
when x is AnInteger(i)
    say("It was $i")
is TwoWords(x, y)
    say("It was $x and $y")
is Nothing
    say("It was nothing")
```

Pattern matching blocks are always checked for exhaustiveness, but you can add
an `else` block to handle all unmatched patterns.

## Tag Checking

Tags can also be quickly checked using the `.TagName` field:

```tomo
assert a.AnInteger != none
assert a.TwoWords == none
```

## Reducing Boilerplate

There are three main areas where we can easily reduce the amount of boilerplate
around enums. We don't need to type `VariousThings.` in front of enum values
when we already know what type of enum we're dealing with. This means that we
don't need the name of the type for pattern matching (because we can infer the
type of the expression being matched). We also don't need the name of the type
when calling a function with an enum argument, nor when returning an enum value
from a function with an explicit return type:

```tomo
enum ArgumentType(AnInt(x:Int), SomeText(text:Text))
enum ReturnType(AnInt(x:Int), Nothing)

func increment(arg:ArgumentType -> ReturnType)
    when arg is AnInt(x)
        return AnInt(x + 1)
    is SomeText
        return Nothing

...

assert increment(AnInt(5)) == AnInt(6)
assert increment(SomeText("HI")) == Nothiing
```

This lets us have overlapping tag names for different types, but smartly infer
which enum's value is being created when we know what we're expecting to get.
This also works for variable assignment to a variable whose type is already
known.

## Namespacing

Enums can also define their own methods and variables inside their namespace:

```tomo
enum VariousThings(AnInteger(i:Int), TwoWords(word1, word2:Text), Nothing)
    meaningful_thing := AnInteger(42)
    func doop(v:VariousThings)
        say("$v")
```

Functions defined in an enum's namespace can be invoked as methods with `:` if
the first argument is the enum's type or a pointer to one (`vt.doop()`).

## Anonymous Enums

In some cases, you may want to use anonymous inline-defined enums. This lets
you define a lightweight type without a name for cases where that's more
convenient. For example, a function that has a simple variant for an argument:

```tomo
func pad_text(text:Text, width:Int, align:enum(Left,Right,Center) = Left -> Text)
    ...
...
padded := pad_text(text, 10, Right)
```

This could be defined explicitly as `enum TextAlignment(Left,Right,Center)` with
`pad_text` defining `align:TextAlignment`, but this adds a new symbol to the
top-level scope and forces the user to think about which name is being used. In
some applications, that overhead is not necessary or desirable.

Anonymous enums can be used in any place where a type is specified:

- Declarations: `my_variable : enum(A, B, C) = A`
- Function arguments: `func foo(arg:enum(A, B, C))`
- Function return values: `func foo(x:Int -> enum(Valid(result:Int), Invalid(reason:Text)))`
- Struct members: `struct Foo(x:enum(A,B,C))`, `enum Baz(Thing(type:enum(A,B,C)))`

In general, anonymous enums should be used sparingly in cases where there are
only a small number of options and the enum code is short. If you expect users
to refer to the enum type, it ought to be defined with a proper name. In the
`pad_text` example, the anonymous enum would cause problems if you wanted to
make a wrapper around it, because you would not be able to refer to the
`pad_text` `align` argument's type:

```tomo
func pad_text_wrapper(text:Text, width:Int, align:???)
    ...pad_text(text, width, align)...
```

**Note:** Each enum type is distinct, regardless of whether the enum shares the
same values with another enum, so you can't define another enum with the same
values and use that in places where a different anonymous enum is expected.


## Result Type

One very common pattern for enums is something which can either succeed or fail
with an informative message. For example, if you try to delete a file, you will
either succeed or fail, and if you fail, you might want to know that it was
because the file doesn't exist or if you don't have permission to delete it.
For this common pattern, Tomo includes a `Result` enum type in the standard
library:

```
enum Result(Success, Failure(reason:Text))
```

You're free to define your own similar enum type or reuse this one as you see
fit.


## Field Access

In some cases, a full `when` block is overkill when a value is assumed to have
a certain tag. In those cases, you can access the enum's tag value using field
access. The resulting value is `none` if the enum value is not the expected tag,
otherwise it will hold the struct contents of the enum value for the given tag.

```tomo
func maybe_fail(should_fail:Bool -> Result)
    if should_fail
        return Failure("It failed")
    else
        return Success

>> maybe_fail(yes).Failure
# Prints 'Failure("It failed")'
assert maybe_fail(yes).Failure!.text == "It failed"

>> maybe_fail(no).Failure
# Prints 'none'
```

## Enum Assertions

In general, it's best to always handle failure results close to the call site
where they occurred. However, sometimes, there's simply nothing you can do
beyond reporting the error to the user and closing the program.

```tomo
result := (/tmp/log.txt).append(msg)
when result is Failure(msg)
    fail(msg)
is Success
    pass
```

For these cases, you can reduce the amount of code using a couple of
simplifications. Firstly, you can access `.Success` to get the optional empty
value of the Result enum (or `none` if there was a failure) and use `!` to
assert that the value is non-`none`.

```tomo
(/tmp/log.txt).append(msg).Success!
```

Tomo is smart enough to give you a good error message in this case that will
look something like:

```
This was expected to be Success, but it was:
Failure("Could not write to file: /tmp/log.txt (Permission denied)")
```

You can further reduce the verbosity of this code by applying the `!` directly
to the Result enum value:

```tomo
(/tmp/log.txt).append(msg)!
```

When the `!` operator is applied to an enum value, the effect is the same as
applying `.Success!` or whatever the first tag in the enum definition is.

```tomo
enum Foo(A(member:Int), B)

f := Foo.A(123)
assert f! == f.A!
assert f!.member == 123
```
