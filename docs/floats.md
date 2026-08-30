# Floating point numbers

Tomo has two hardware floating point types: `Float64` (64-bit, AKA `double`)
and `Float32` (32-bit, AKA `float`). They are what to reach for when speed
matters more than exactness; the default numeric type is [`Num`](nums.md),
which is exact.

Floats support the standard math operations (`x+y`, `x-y`, `x*y`, `x/y`) as
well as powers/exponentiation (`x^y`), modulus (`x mod y` and `x mod1 y`), and
floored division (`x // y`, which is `floor(x/y)`, unlike the integer types
and `Num`, whose `//` is the Euclidean quotient).

Because a bare numeric literal like `1.5` is a `Num`, a float is written by
naming the type: `Float64(1.5)` or `Float32(123.456)`. In any position where
the type is already known, whether a typed variable, a function argument, or a
comparison against a float, a literal is compiled directly to a float with
no `Num` involved:

```tomo
x : Float64 = 1.5      # compiled as a float64 literal

func doop(n:Float64 -> Float64)
    return n * 2

assert doop(0.5) == 1.0    # 0.5 is compiled as a float64 literal
```

Converting a `Num` to a float approximates it, which is what asking for a
float means, so `truncate` defaults to `yes`. Pass `truncate=no` to demand
that the float be the exact value:

```tomo
assert Float64(1./3.).near(0.333333)
assert Float64(0.5, truncate=no) == 0.5   # a half is exactly a float
# Float64(1./3., truncate=no) would fail: a third is not
```

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
called `none` and has the type `Float64?` or `Float32?`. In this way, it's no
different from optional integers or optional lists. This means that if a
variable has type `Float64`, it is guaranteed to not hold a NaN value. This also
means that operations which may produce NaN values have a result type of
`Float64?`. For example, division can take two non-NaN values and return a result
that is NaN (zero divided by zero). Similarly, multiplication can produce NaN
values (zero times infinity), and many math functions like `sqrt()` can return
NaN for some inputs.

Unfortunately, one of the big downsides of optional types is that explicit
`none` handling can be very verbose. To make floats actually usable, Tomo applies
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

# Optional floats can be handled explicitly using `or` and `!`:
assert zero/zero or -123 == -123

# This would raise a runtime error if `zero` and `y` were zero:
assert (zero/y)! == 0

# Assigning to a non-optional variable will do an implicit check for none and
# raise a runtime error if the value is none, essentially the same as an
# implicit `!`:
zero = zero/y

func doop(x:Float64 -> Float64)
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

[API documentation](../api/floats.md)

See also [Num](nums.md), the exact default numeric type.
