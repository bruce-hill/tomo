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
