# Nums

Tomo has two floating point number types: `Num` (64-bit, AKA `double`) and
`Num32` (32-bit, AKA `float`). Num literals can have a decimal point (e.g.
`5.`), a scientific notation suffix (e.g. `1e8`) or a percent sign. Numbers
that end in a percent sign are divided by 100 at compile time (i.e. `5% ==
0.05`). Numbers can also use the `deg` suffix to represent degrees, which
are converted to radians at compile time (i.e. `180deg == Nums.PI`).

Nums support the standard math operations (`x+y`, `x-y`, `x*y`, `x/y`) as well as
powers/exponentiation (`x^y`) and modulus (`x mod y` and `x mod1 y`).

32-bit numbers can be constructed using the type name: `Num32(123.456)`.

## NaN

IEEE-754 floating point numbers define a concept call `NaN` (Not a Number),
which is the result value used to signal various operations (e.g. `0/0`) that
have no mathematically defined result value. NaNs are implemented at the
hardware level and propagate through floating point operations. This allows you
to perform many chained operations on the assumption that it's unlikely to have
NaN values, and only perform checks at the end of the chain of operations,
instead of performing checks after each operation. Unfortunately, it's also
easy to forget to perform any checks at all because most type systems don't
differentiate between possibly-NaN values and definitely-not-NaN values.

Tomo has a separate concept for expressing the lack of a defined value:
optional types. Consequently, Tomo has merged these two concepts, so `NaN` is
called `none` and has the type `Num?` or `Num32?`. In this way, it's no
different from optional integers or optional lists. This means that if a
variable has type `Num`, it is guaranteed to not hold a NaN value. This also
means that operations which may produce NaN values have a result type of
`Num?`. For example, division can take two non-NaN values and return a result
that is NaN (zero divided by zero). Similarly, multiplication can produce NaN
values (zero times infinity), and many math functions like `sqrt()` can return
NaN for some inputs.

Unfortunately, one of the big downsides of optional types is that explicit
`none` handling can be very verbose. To make Nums actually usable, Tomo applies
very liberal use of type coercion and implicit `none` checks when values are
required to be non-none. Here are a few examples:

```tomo
zero := 0.0
assert zero == 0

y := 1.0

# Division might produce none:
assert zero / y == 0
assert zero / zero == none

# Optional types and none values propagate:
assert zero/y + 1 + 2 == 3
assert zero/zero + 1 + 2 == none

# Optional Nums can be handled explicitly using `or` and `!`:
assert zero/zero or -123 == -123

# This would raise a runtime error if `zero` and `y` were zero:
assert (zero/y)! == 0

# Assigning to a non-optional variable will do an implicit check for none and
# raise a runtime error if the value is none, essentially the same as an
# implicit `!`:
zero = zero/y

func doop(x:Num -> Num)
    # If a function's return type is non-optional and an optional value is
    # used in a return statement, an implicit none check will be inserted and
    # will error if the value is none:
    return zero / 2

# Function arguments are also implicitly checked for none if the given value
# is optional and the function needs a non-optional value:
assert doop(zero/y) == 0
```

Hopefully the end result of this system is one where users can take advantage
of the performance benefits of hardware NaN propagation, while still having the
compiler enforce checking for undefined values. Users who don't want automatic
NaN-checking can use optional types and explicit checks where necessary. By
default, automatic NaN-checking happens at interface boundaries (function
arguments, return values, and variable assignments), so NaN values should be
caught early when an error message would have helpful context, while
eliminating conditional branching inside of compound math expressions. Users
should also be able to write code that can safely assume that all values
provided are not NaN.

# API

[API documentation](../api/nums.md)
