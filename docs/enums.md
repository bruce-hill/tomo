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

The values inside an enum can be accessed with pattern matching:

```tomo
when x is AnInteger(i):
    say("It was $i")
is TwoWords(x, y):
    say("It was $x and $y")
is Nothing:
    say("It was nothing")
```

Pattern matching blocks are always checked for exhaustiveness, but you can add
an `else` block to handle all unmatched patterns.

## Tag Checking

Tags can also be quickly checked using the `.TagName` field:

```tomo
>> a.AnInteger
= yes
>> a.TwoWords
= no
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
enum ReturnType(Nothing, AnInt(x:Int))

func increment(arg:ArgumentType -> ReturnType):
    when arg is AnInt(x):
        return AnInt(x + 1)
    is SomeText:
        return Nothing

...

>> increment(AnInt(5))
= AnInt(6)
>> increment(SomeText("HI"))
= Nothiing
```

This lets us have overlapping tag names for different types, but smartly infer
which enum's value is being created when we know what we're expecting to get.
This also works for variable assignment to a variable whose type is already
known.

## Namespacing

Enums can also define their own methods and variables inside their namespace:

```tomo
enum VariousThings(AnInteger(i:Int), TwoWords(word1, word2:Text), Nothing):
    meaningful_thing := AnInteger(42)
    func doop(v:VariousThings):
        say("$v")
```

Functions defined in an enum's namespace can be invoked as methods with `:` if
the first argument is the enum's type or a pointer to one (`vt:doop()`).
