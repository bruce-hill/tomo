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
>> x := 0.0
= 0 : Num

y := 1.0

# Division might produce none:
>> x / y
= 0 : Num?
>> x / x
= none : Num?

# Optional types and none values propagate:
>> x/y + 1 + 2
= 3 : Num?
>> x/x + 1 + 2
= none : Num?

# Optional Nums can be handled explicitly using `or` and `!`:
>> x/x or -123
= -123 : Num

# This would raise a runtime error if `x` and `y` were zero:
>> (x/y)!
= 0 : Num

# Assigning to a non-optional variable will do an implicit check for none and
# raise a runtime error if the value is none, essentially the same as an
# implicit `!`:
x = x/y

func doop(x:Num -> Num):
    # If a function's return type is non-optional and an optional value is
    # used in a return statement, an implicit none check will be inserted and
    # will error if the value is none:
    return x / 2

# Function arguments are also implicitly checked for none if the given value
# is optional and the function needs a non-optional value:
>> doop(x/y)
= 0 : Num
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

## Constants

- **`1_PI`**: \( \frac{1}{\pi} \)
- **`2_PI`**: \( 2 \times \pi \)
- **`2_SQRTPI`**: \( 2 \times \sqrt{\pi} \)
- **`E`**: Base of natural logarithms (\( e \))
- **`INF`**: Positive infinity
- **`LN10`**: Natural logarithm of 10
- **`LN2`**: Natural logarithm of 2
- **`LOG2E`**: Logarithm base 2 of \( e \)
- **`PI`**: Pi (\( \pi \))
- **`PI_2`**: \( \frac{\pi}{2} \)
- **`PI_4`**: \( \frac{\pi}{4} \)
- **`SQRT1_2`**: \( \sqrt{\frac{1}{2}} \)
- **`SQRT2`**: \( \sqrt{2} \)
- **`TAU`**: Tau (\( 2 \times \pi \))

## Functions

Each Num type has its own version of the following functions. Functions can be
called either on the type itself: `Num.sqrt(x)` or as a method call:
`x.sqrt()`. Method call syntax is preferred.

---

- [`func abs(n: Num -> Num)`](#abs)
- [`func acos(x: Num -> Num)`](#acos)
- [`func acosh(x: Num -> Num)`](#acosh)
- [`func asin(x: Num -> Num)`](#asin)
- [`func asinh(x: Num -> Num)`](#asinh)
- [`func atan(x: Num -> Num)`](#atan)
- [`func atan2(x: Num, y: Num -> Num)`](#atan2)
- [`func atanh(x: Num -> Num)`](#atanh)
- [`func cbrt(x: Num -> Num)`](#cbrt)
- [`func ceil(x: Num -> Num)`](#ceil)
- [`func clamped(x, low, high: Num -> Num)`](#clamped)
- [`func copysign(x: Num, y: Num -> Num)`](#copysign)
- [`func cos(x: Num -> Num)`](#cos)
- [`func cosh(x: Num -> Num)`](#cosh)
- [`func erf(x: Num -> Num)`](#erf)
- [`func erfc(x: Num -> Num)`](#erfc)
- [`func exp(x: Num -> Num)`](#exp)
- [`func exp2(x: Num -> Num)`](#exp2)
- [`func expm1(x: Num -> Num)`](#expm1)
- [`func fdim(x: Num, y: Num -> Num)`](#fdim)
- [`func floor(x: Num -> Num)`](#floor)
- [`func format(n: Num, precision: Int = 0 -> Text)`](#format)
- [`func hypot(x: Num, y: Num -> Num)`](#hypot)
- [`func isfinite(n: Num -> Bool)`](#isfinite)
- [`func isinf(n: Num -> Bool)`](#isinf)
- [`func j0(x: Num -> Num)`](#j0)
- [`func j1(x: Num -> Num)`](#j1)
- [`func log(x: Num -> Num)`](#log)
- [`func log10(x: Num -> Num)`](#log10)
- [`func log1p(x: Num -> Num)`](#log1p)
- [`func log2(x: Num -> Num)`](#log2)
- [`func logb(x: Num -> Num)`](#logb)
- [`func mix(amount: Num, x: Num, y: Num -> Num)`](#mix)
- [`func near(x: Num, y: Num, ratio: Num = 1e-9, min_epsilon: Num = 1e-9 -> Bool)`](#near)
- [`func nextafter(x: Num, y: Num -> Num)`](#nextafter)
- [`func parse(text: Text -> Num?)`](#parse)
- [`func percent(n: Num -> Text)`](#percent)
- [`func rint(x: Num -> Num)`](#rint)
- [`func round(x: Num -> Num)`](#round)
- [`func scientific(n: Num, precision: Int = 0 -> Text)`](#scientific)
- [`func significand(x: Num -> Num)`](#significand)
- [`func sin(x: Num -> Num)`](#sin)
- [`func sinh(x: Num -> Num)`](#sinh)
- [`func sqrt(x: Num -> Num)`](#sqrt)
- [`func tan(x: Num -> Num)`](#tan)
- [`func tanh(x: Num -> Num)`](#tanh)
- [`func tgamma(x: Num -> Num)`](#tgamma)
- [`func trunc(x: Num -> Num)`](#trunc)
- [`func y0(x: Num -> Num)`](#y0)
- [`func y1(x: Num -> Num)`](#y1)

### `abs`
Calculates the absolute value of a number.

```tomo
func abs(n: Num -> Num)
```

- `n`: The number whose absolute value is to be computed.

**Returns:**
The absolute value of `n`.

**Example:**
```tomo
>> (-3.5).abs()
= 3.5
```

---

### `acos`
Computes the arc cosine of a number.

```tomo
func acos(x: Num -> Num)
```

- `x`: The number for which the arc cosine is to be calculated.

**Returns:**
The arc cosine of `x` in radians.

**Example:**
```tomo
>> (0.0).acos() // -> (π/2)
= 1.5708
```

---

### `acosh`
Computes the inverse hyperbolic cosine of a number.

```tomo
func acosh(x: Num -> Num)
```

- `x`: The number for which the inverse hyperbolic cosine is to be calculated.

**Returns:**
The inverse hyperbolic cosine of `x`.

**Example:**
```tomo
>> (1.0).acosh()
= 0
```

---

### `asin`
Computes the arc sine of a number.

```tomo
func asin(x: Num -> Num)
```

- `x`: The number for which the arc sine is to be calculated.

**Returns:**
The arc sine of `x` in radians.

**Example:**
```tomo
>> (0.5).asin()  // -> (π/6)
= 0.5236
```

---

### `asinh`
Computes the inverse hyperbolic sine of a number.

```tomo
func asinh(x: Num -> Num)
```

- `x`: The number for which the inverse hyperbolic sine is to be calculated.

**Returns:**
The inverse hyperbolic sine of `x`.

**Example:**
```tomo
>> (0.0).asinh()
= 0
```

---

### `atan`
Computes the arc tangent of a number.

```tomo
func atan(x: Num -> Num)
```

- `x`: The number for which the arc tangent is to be calculated.

**Returns:**
The arc tangent of `x` in radians.

**Example:**
```tomo
>> (1.0).atan() // -> (π/4)
= 0.7854
```

---

### `atan2`
Computes the arc tangent of the quotient of two numbers.

```tomo
func atan2(x: Num, y: Num -> Num)
```

- `x`: The numerator.
- `y`: The denominator.

**Returns:**
The arc tangent of `x/y` in radians.

**Example:**
```tomo
>> Num.atan2(1, 1) // -> (π/4)
= 0.7854
```

---

### `atanh`
Computes the inverse hyperbolic tangent of a number.

```tomo
func atanh(x: Num -> Num)
```

- `x`: The number for which the inverse hyperbolic tangent is to be calculated.

**Returns:**
The inverse hyperbolic tangent of `x`.

**Example:**
```tomo
>> (0.5).atanh()
= 0.5493
```

---

### `cbrt`
Computes the cube root of a number.

```tomo
func cbrt(x: Num -> Num)
```

- `x`: The number for which the cube root is to be calculated.

**Returns:**
The cube root of `x`.

**Example:**
```tomo
>> (27.0).cbrt()
= 3
```

---

### `ceil`
Rounds a number up to the nearest integer.

```tomo
func ceil(x: Num -> Num)
```

- `x`: The number to be rounded up.

**Returns:**
The smallest integer greater than or equal to `x`.

**Example:**
```tomo
>> (3.2).ceil()
= 4
```

---

### `clamped`
Returns the given number clamped between two values so that it is within
that range.

```tomo
clamped(x, low, high: Num -> Num)
```

- `x`: The number to clamp.
- `low`: The lowest value the result can take.
- `high`: The highest value the result can take.

**Returns:**  
The first argument clamped between the other two arguments.

**Example:**  
```tomo
>> (2.5).clamped(5.5, 10.5)
= 5.5
```

---

### `copysign`
Copies the sign of one number to another.

```tomo
func copysign(x: Num, y: Num -> Num)
```

- `x`: The number whose magnitude will be copied.
- `y`: The number whose sign will be copied.

**Returns:**
A number with the magnitude of `x` and the sign of `y`.

**Example:**
```tomo
>> (3.0).copysign(-1)
= -3
```

---

### `cos`
Computes the cosine of a number (angle in radians).

```tomo
func cos(x: Num -> Num)
```

- `x`: The angle in radians.

**Returns:**
The cosine of `x`.

**Example:**
```tomo
>> (0.0).cos()
= 1
```

---

### `cosh`
Computes the hyperbolic cosine of a number.

```tomo
func cosh(x: Num -> Num)
```

- `x`: The number for which the hyperbolic cosine is to be calculated.

**Returns:**
The hyperbolic cosine of `x`.

**Example:**
```tomo
>> (0.0).cosh()
= 1
```

---

### `erf`
Computes the error function of a number.

```tomo
func erf(x: Num -> Num)
```

- `x`: The number for which the error function is to be calculated.

**Returns:**
The error function of `x`.

**Example:**
```tomo
>> (0.0).erf()
= 0
```

---

### `erfc`
Computes the complementary error function of a number.

```tomo
func erfc(x: Num -> Num)
```

- `x`: The number for which the complementary error function is to be calculated.

**Returns:**
The complementary error function of `x`.

**Example:**
```tomo
>> (0.0).erfc()
= 1
```

---

### `exp`
Computes the exponential function \( e^x \) for a number.

```tomo
func exp(x: Num -> Num)
```

- `x`: The exponent.

**Returns:**
The value of \( e^x \).

**Example:**
```tomo
>> (1.0).exp()
= 2.7183
```

---

### `exp2`
Computes \( 2^x \) for a number.

```tomo
func exp2(x: Num -> Num)
```

- `x`: The exponent.

**Returns:**
The value of \( 2^x \).

**Example:**
```tomo
>> (3.0).exp2()
= 8
```

---

### `expm1`
Computes \( e^x - 1 \) for a number.

```tomo
func expm1(x: Num -> Num)
```

- `x`: The exponent.

**Returns:**
The value of \( e^x - 1 \).

**Example:**
```tomo
>> (1.0).expm1()
= 1.7183
```

---

### `fdim`
Computes the positive difference between two numbers.

```tomo
func fdim(x: Num, y: Num -> Num)
```

- `x`: The first number.
- `y`: The second number.

**Returns:**
The positive difference \( \max(0, x - y) \).

**Example:**
```tomo
fd

>> (5.0).fdim(3)
= 2
```

---

### `floor`
Rounds a number down to the nearest integer.

```tomo
func floor(x: Num -> Num)
```

- `x`: The number to be rounded down.

**Returns:**
The largest integer less than or equal to `x`.

**Example:**
```tomo
>> (3.7).floor()
= 3
```

---

### `format`
Formats a number as a text with a specified precision.

```tomo
func format(n: Num, precision: Int = 0 -> Text)
```

- `n`: The number to be formatted.
- `precision`: The number of decimal places. Default is `0`.

**Returns:**
A text representation of the number with the specified precision.

**Example:**
```tomo
>> (3.14159).format(precision=2)
= "3.14"
```

---

### `hypot`
Computes the Euclidean norm, \( \sqrt{x^2 + y^2} \), of two numbers.

```tomo
func hypot(x: Num, y: Num -> Num)
```

- `x`: The first number.
- `y`: The second number.

**Returns:**
The Euclidean norm of `x` and `y`.

**Example:**
```tomo
>> Num.hypot(3, 4)
= 5
```

---

### `isfinite`
Checks if a number is finite.

```tomo
func isfinite(n: Num -> Bool)
```

- `n`: The number to be checked.

**Returns:**
`yes` if `n` is finite, `no` otherwise.

**Example:**
```tomo
>> (1.0).isfinite()
= yes
>> Num.INF.isfinite()
= no
```

---

### `isinf`
Checks if a number is infinite.

```tomo
func isinf(n: Num -> Bool)
```

- `n`: The number to be checked.

**Returns:**
`yes` if `n` is infinite, `no` otherwise.

**Example:**
```tomo
>> Num.INF.isinf()
= yes
>> (1.0).isinf()
= no
```

---

### `j0`
Computes the Bessel function of the first kind of order 0.

```tomo
func j0(x: Num -> Num)
```

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the first kind of order 0 of `x`.

**Example:**
```tomo
>> (0.0).j0()
= 1
```

---

### `j1`
Computes the Bessel function of the first kind of order 1.

```tomo
func j1(x: Num -> Num)
```

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the first kind of order 1 of `x`.

**Example:**
```tomo
>> (0.0).j1()
= 0
```

---

### `log`
Computes the natural logarithm (base \( e \)) of a number.

```tomo
func log(x: Num -> Num)
```

- `x`: The number for which the natural logarithm is to be calculated.

**Returns:**
The natural logarithm of `x`.

**Example:**
```tomo
>> Num.E.log()
= 1
```

---

### `log10`
Computes the base-10 logarithm of a number.

```tomo
func log10(x: Num -> Num)
```

- `x`: The number for which the base-10 logarithm is to be calculated.

**Returns:**
The base-10 logarithm of `x`.

**Example:**
```tomo
>> (100.0).log10()
= 2
```

---

### `log1p`
Computes \( \log(1 + x) \) for a number.

```tomo
func log1p(x: Num -> Num)
```

- `x`: The number for which \( \log(1 + x) \) is to be calculated.

**Returns:**
The value of \( \log(1 + x) \).

**Example:**
```tomo
>> (1.0).log1p()
= 0.6931
```

---

### `log2`
Computes the base-2 logarithm of a number.

```tomo
func log2(x: Num -> Num)
```

- `x`: The number for which the base-2 logarithm is to be calculated.

**Returns:**
The base-2 logarithm of `x`.

**Example:**
```tomo
>> (8.0).log2()
= 3
```

---

### `logb`
Computes the binary exponent (base-2 logarithm) of a number.

```tomo
func logb(x: Num -> Num)
```

- `x`: The number for which the binary exponent is to be calculated.

**Returns:**
The binary exponent of `x`.

**Example:**
```tomo
>> (8.0).logb()
= 3
```

---

### `mix`
Interpolates between two numbers based on a given amount.

```tomo
func mix(amount: Num, x: Num, y: Num -> Num)
```

- `amount`: The interpolation factor (between `0` and `1`).
- `x`: The starting number.
- `y`: The ending number.

**Returns:**
The interpolated number between `x` and `y` based on `amount`.

**Example:**
```tomo
>> (0.5).mix(10, 20)
= 15
>> (0.25).mix(10, 20)
= 12.5
```

---

### `near`
Checks if two numbers are approximately equal within specified tolerances. If
two numbers are within an absolute difference or the ratio between the two is
small enough, they are considered near each other.

```tomo
func near(x: Num, y: Num, ratio: Num = 1e-9, min_epsilon: Num = 1e-9 -> Bool)
```

- `x`: The first number.
- `y`: The second number.
- `ratio`: The relative tolerance. Default is `1e-9`.
- `min_epsilon`: The absolute tolerance. Default is `1e-9`.

**Returns:**
`yes` if `x` and `y` are approximately equal within the specified tolerances, `no` otherwise.

**Example:**
```tomo
>> (1.0).near(1.000000001)
= yes

>> (100.0).near(110, ratio=0.1)
= yes

>> (5.0).near(5.1, min_epsilon=0.1)
= yes
```

---

### `nextafter`
Computes the next representable value after a given number towards a specified direction.

```tomo
func nextafter(x: Num, y: Num -> Num)
```

- `x`: The starting number.
- `y`: The direction towards which to find the next representable value.

**Returns:**
The next representable value after `x` in the direction of `y`.

**Example:**
```tomo
>> (1.0).nextafter(1.1)
= 1.0000000000000002
```

---

### `parse`
Converts a text representation of a number into a floating-point number.

```tomo
func parse(text: Text -> Num?)
```

- `text`: The text containing the number.

**Returns:**
The number represented by the text or `none` if the entire text can't be parsed
as a number.

**Example:**
```tomo
>> Num.parse("3.14")
= 3.14
>> Num.parse("1e3")
= 1000
```

---

### `percent`
Convert a number into a percentage text with a percent sign.

```tomo
func percent(n: Num, precision: Int = 0 -> Text)
```

- `n`: The number to be converted to a percent.
- `precision`: The number of decimal places. Default is `0`.

**Returns:**
A text representation of the number as a percentage with a percent sign.

**Example:**
```tomo
>> (0.5).percent()
= "50%"
>> (1./3.).percent(2)
= "33.33%"
```

---

### `rint`
Rounds a number to the nearest integer, with ties rounded to the nearest even integer.

```tomo
func rint(x: Num -> Num)
```

- `x`: The number to be rounded.

**Returns:**
The nearest integer value of `x`.

**Example:**
```tomo
>> (3.5).rint()
= 4
>> (2.5).rint()
= 2
```

---

### `round`
Rounds a number to the nearest whole number integer.

```tomo
func round(x: Num -> Num)
```

- `x`: The number to be rounded.

**Returns:**
The nearest integer value of `x`.

**Example:**
```tomo
>> (2.3).round()
= 2
>> (2.7).round()
= 3
```

---

### `scientific`
Formats a number in scientific notation with a specified precision.

```tomo
func scientific(n: Num, precision: Int = 0 -> Text)
```

- `n`: The number to be formatted.
- `precision`: The number of decimal places. Default is `0`.

**Returns:**
A text representation of the number in scientific notation with the specified precision.

**Example:**
```tomo
>> (12345.6789).scientific(precision=2)
= "1.23e+04"
```

---

### `significand`
Extracts the significand (or mantissa) of a number.

```tomo
func significand(x: Num -> Num)
```

- `x`: The number from which to extract the significand.

**Returns:**
The significand of `x`.

**Example:**
```tomo
>> (1234.567).significand()
= 0.1234567
```

---

### `sin`
Computes the sine of a number (angle in radians).

```tomo
func sin(x: Num -> Num)
```

- `x`: The angle in radians.

**Returns:**
The sine of `x`.

**Example:**
```tomo
>> (0.0).sin()
= 0
```

---

### `sinh`
Computes the hyperbolic sine of a number.

```tomo
func sinh(x: Num -> Num)
```

- `x`: The number for which the hyperbolic sine is to be calculated.

**Returns:**
The hyperbolic sine of `x`.

**Example:**
```tomo
>> (0.0).sinh()
= 0
```

---

### `sqrt`
Computes the square root of a number.

```tomo
func sqrt(x: Num -> Num)
```

- `x`: The number for which the square root is to be calculated.

**Returns:**
The square root of `x`.

**Example:**
```tomo
>> (16.0).sqrt()
= 4
```

---

### `tan`
Computes the tangent of a number (angle in radians).

```tomo
func tan(x: Num -> Num)
```

- `x`: The angle in radians.

**Returns:**
The tangent of `x`.

**Example:**
```tomo
>> (0.0).tan()
= 0
```

---

### `tanh`
Computes the hyperbolic tangent of a number.

```tomo
func tanh(x: Num -> Num)
```

- `x`: The number for which the hyperbolic tangent is to be calculated.

**Returns:**
The hyperbolic tangent of `x`.

**Example:**
```tomo
>> (0.0).tanh()
= 0
```

---

### `tgamma`
Computes the gamma function of a number.

```tomo
func tgamma(x: Num -> Num)
```

- `x`: The number for which the gamma function is to be calculated.

**Returns:**
The gamma function of `x`.

**Example:**
```tomo
>> (1.0).tgamma()
= 1
```

---

### `trunc`
Truncates a number to the nearest integer towards zero.

```tomo
func trunc(x: Num -> Num)
```

- `x`: The number to be truncated.

**Returns:**
The integer part of `x` towards zero.

**Example:**
```tomo
>> (3.7).trunc()
= 3
>> (-3.7).trunc()
= -3
```

---

### `y0`
Computes the Bessel function of the second kind of order 0.

```tomo
func y0(x: Num -> Num)
```

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the second kind of order 0 of `x`.

**Example:**
```tomo
>> (1.0).y0()
= -0.7652
```

---

### `y1`
Computes the Bessel function of the second kind of order 1.

```tomo
func y1(x: Num -> Num)
```

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the second kind of order 1 of `x`.

**Example:**
```tomo
>> (1.0).y1()
= 0.4401
```
