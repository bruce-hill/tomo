# Integers

Tomo has five types of integers:

- `Int`: the default integer type, which uses an efficient tagged 29-bit
  integer value for small numbers, and falls back to a bigint implementation
  when values are too large to fit in 29-bits. The bigint implementation uses
  the GNU MP library. These integers are fast for small numbers and guaranteed
  to always be correct and never overflow.
- `Int8`/`Int16`/`Int32`/`Int64`: Fixed-size integers that take up `N` bits.
  These integers must be explicitly constructed using their type name (e.g.
  `Int64(5)`) and are subject to overflowing on arithmetic operations. If an
  overflow occurs, a runtime error will be raised.
- In cases where it is possible to infer that an integer literal should be
  used as a fixed-size integer, the literal will be converted at compile time
  to the appropriate fixed-size integer type and checked to ensure that it
  can fit in the needed size. For example, if you declare a variable as
  `x := Int64(0)` and later do `x + 1`, it's inferred that the `1` is a 64-bit
  integer literal.

Runtime conversion between integer types (casting) can be done explicitly by
calling the target type as a function: `Int32(x)`. For fixed-width types, the
conversion function also accepts a second parameter, `truncate`. If `truncate`
is `no` (the default), conversion will create a runtime error if the value is
too large to fit in the target type. If `truncate` is `yes`, then the resulting
value will be a truncated form of the input value.

Integers support the standard math operations (`x+y`, `x-y`, `x*y`, `x/y`) as
well as powers/exponentiation (`x^y`), modulus (`x mod y` and `x mod1 y`), and
bitwise operations: `x and y`, `x or y`, `x xor y`, `x << y`, `x >> y`, `x >>>
y` (unsigned right shift), and `x <<< y` (unsighted left shift). The operators
`and`, `or`, and `xor` are _bitwise_, not logical operators.

## Integer Literals

The simplest form of integer literal is a string of digits, which is inferred
to have type `Int` (unbounded size).

```tomo
>>> 123456789012345678901234567890
= 123456789012345678901234567890 : Int
```

Underscores may also be used to visually break up the integer for readability:

```tomo
a_million := 1_000_000
```

Hexadecimal, octal, and binary integer literals are also supported:

```tomo
hex := 0x123F
octal := 0o644
binary := 0b10101
```

For fixed-sized integers, use the type's name as a constructor:

```tomo
my_int64 := Int64(12345)
my_int32 := Int32(12345)
my_int16 := Int32(12345)
my_int8 := Int32(123)
```

A compiler error will be raised if you attempt to construct a value that cannot
fit in the specified integer size (e.g. `Int8(99999)`).

## A Note on Division

Unlike some other languages (including C), Tomo uses a mathematically
consistent definition of division called [Euclidean
Division](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf)
that upholds the following invariants for all inputs:

```tomo
quotient := numerator / denominator
remainder := numerator mod denominator

# Modulus always gives a non-negative result:
>> remainder >= 0
= yes

# The numerator can be reconstructed sensibly:
>> numerator == denominator * quotient + remainder
= yes
```

Importantly, these invariants hold for both positive and negative numerators
and denominators. When the numerator and denominator are both positive, you
will not notice any difference from how integer division and modulus work in
other programming languages. However, the behavior is a bit different when
negative numbers are involved. Integer division rounds _down_ instead of
rounding _towards zero_, and modulus never gives negative results:

```tomo
>> quotient := -1 / 5
= -1

>> remainder := -1 mod 5
= 4

>> -1 == 5 * -1 + 4
= yes
```

```tomo
>> quotient := 16 / -5
= -3

>> remainder := -1 mod 5
= 1

>> 16 == -5 * -3 + 1
= yes
```

# API

[API documentation](../api/integers.md)
