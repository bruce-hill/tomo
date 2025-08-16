% API

# Builtins

# Num
## Num.1_PI

```tomo
Num.1_PI : Num
```

The constant $\frac{1}{\pi}$.

## Num.2_PI

```tomo
Num.2_PI : Num
```

The constant $2 \times \pi$.

## Num.2_SQRTPI

```tomo
Num.2_SQRTPI : Num
```

The constant $2 \times \sqrt{\pi}$.

## Num.E

```tomo
Num.E : Num
```

The base of the natural logarithm ($e$).

## Num.INF

```tomo
Num.INF : Num
```

Positive infinity.

## Num.LN10

```tomo
Num.LN10 : Num
```

The natural logarithm of 10.

## Num.LN2

```tomo
Num.LN2 : Num
```

The natural logarithm of 2.

## Num.LOG2E

```tomo
Num.LOG2E : Num
```

The base 2 logarithm of $e$

## Num.PI

```tomo
Num.PI : Num
```

Pi ($\pi$).

## Num.PI_2

```tomo
Num.PI_2 : Num
```

$\frac{\pi}{2}$

## Num.PI_4

```tomo
Num.PI_4 : Num
```

$\frac{\pi}{4}$

## Num.SQRT1_2

```tomo
Num.SQRT1_2 : Num
```

$\sqrt{\frac{1}{2}}$

## Num.SQRT2

```tomo
Num.SQRT2 : Num
```

$\sqrt{2}$

## Num.TAU

```tomo
Num.TAU : Num
```

Tau ($2 \times \pi$)

## Num.abs

```tomo
Num.abs : func(n: Num -> Num)
```

Calculates the absolute value of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number whose absolute value is to be computed.  | -

**Return:** The absolute value of `n`.


**Example:**
```tomo
>> (-3.5).abs()
= 3.5

```
## Num.acos

```tomo
Num.acos : func(x: Num -> Num)
```

Computes the arc cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the arc cosine is to be calculated.  | -

**Return:** The arc cosine of `x` in radians.


**Example:**
```tomo
>> (0.0).acos() // -> (π/2)
= 1.5708

```
## Num.acosh

```tomo
Num.acosh : func(x: Num -> Num)
```

Computes the inverse hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the inverse hyperbolic cosine is to be calculated.  | -

**Return:** The inverse hyperbolic cosine of `x`.


**Example:**
```tomo
>> (1.0).acosh()
= 0

```
## Num.asin

```tomo
Num.asin : func(x: Num -> Num)
```

Computes the arc sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the arc sine is to be calculated.  | -

**Return:** The arc sine of `x` in radians.


**Example:**
```tomo
>> (0.5).asin()  // -> (π/6)
= 0.5236

```
## Num.asinh

```tomo
Num.asinh : func(x: Num -> Num)
```

Computes the inverse hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the inverse hyperbolic sine is to be calculated.  | -

**Return:** The inverse hyperbolic sine of `x`.


**Example:**
```tomo
>> (0.0).asinh()
= 0

```
## Num.atan

```tomo
Num.atan : func(x: Num -> Num)
```

Computes the arc tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the arc tangent is to be calculated.  | -

**Return:** The arc tangent of `x` in radians.


**Example:**
```tomo
>> (1.0).atan() // -> (π/4)
= 0.7854

```
## Num.atan2

```tomo
Num.atan2 : func(x: Num, y: Num -> Num)
```

Computes the arc tangent of the quotient of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The numerator.  | -
y | `Num` | The denominator.  | -

**Return:** The arc tangent of `x/y` in radians.


**Example:**
```tomo
>> Num.atan2(1, 1) // -> (π/4)
= 0.7854

```
## Num.atanh

```tomo
Num.atanh : func(x: Num -> Num)
```

Computes the inverse hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the inverse hyperbolic tangent is to be calculated.  | -

**Return:** The inverse hyperbolic tangent of `x`.


**Example:**
```tomo
>> (0.5).atanh()
= 0.5493

```
## Num.cbrt

```tomo
Num.cbrt : func(x: Num -> Num)
```

Computes the cube root of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the cube root is to be calculated.  | -

**Return:** The cube root of `x`.


**Example:**
```tomo
>> (27.0).cbrt()
= 3

```
## Num.ceil

```tomo
Num.ceil : func(x: Num -> Num)
```

Rounds a number up to the nearest integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to be rounded up.  | -

**Return:** The smallest integer greater than or equal to `x`.


**Example:**
```tomo
>> (3.2).ceil()
= 4

```
## Num.clamped

```tomo
Num.clamped : func(x: Num, low: Num, high: Num -> Num)
```

Returns the given number clamped between two values so that it is within that range.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to clamp.  | -
low | `Num` | The lowest value the result can take.  | -
high | `Num` | The highest value the result can take.  | -

**Return:** The first argument clamped between the other two arguments.


**Example:**
```tomo
>> (2.5).clamped(5.5, 10.5)
= 5.5

```
## Num.copysign

```tomo
Num.copysign : func(x: Num, y: Num -> Num)
```

Copies the sign of one number to another.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose magnitude will be copied.  | -
y | `Num` | The number whose sign will be copied.  | -

**Return:** A number with the magnitude of `x` and the sign of `y`.


**Example:**
```tomo
>> (3.0).copysign(-1)
= -3

```
## Num.cos

```tomo
Num.cos : func(x: Num -> Num)
```

Computes the cosine of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The angle in radians.  | -

**Return:** The cosine of `x`.


**Example:**
```tomo
>> (0.0).cos()
= 1

```
## Num.cosh

```tomo
Num.cosh : func(x: Num -> Num)
```

Computes the hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the hyperbolic cosine is to be calculated.  | -

**Return:** The hyperbolic cosine of `x`.


**Example:**
```tomo
>> (0.0).cosh()
= 1

```
## Num.erf

```tomo
Num.erf : func(x: Num -> Num)
```

Computes the error function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the error function is to be calculated.  | -

**Return:** The error function of `x`.


**Example:**
```tomo
>> (0.0).erf()
= 0

```
## Num.erfc

```tomo
Num.erfc : func(x: Num -> Num)
```

Computes the complementary error function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the complementary error function is to be calculated.  | -

**Return:** The complementary error function of `x`.


**Example:**
```tomo
>> (0.0).erfc()
= 1

```
## Num.exp

```tomo
Num.exp : func(x: Num -> Num)
```

Computes the exponential function $e^x$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The exponent.  | -

**Return:** The value of $e^x$.


**Example:**
```tomo
>> (1.0).exp()
= 2.7183

```
## Num.exp2

```tomo
Num.exp2 : func(x: Num -> Num)
```

Computes $2^x$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The exponent.  | -

**Return:** The value of $2^x$.


**Example:**
```tomo
>> (3.0).exp2()
= 8

```
## Num.expm1

```tomo
Num.expm1 : func(x: Num -> Num)
```

Computes $e^x - 1$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The exponent.  | -

**Return:** The value of $e^x - 1$.


**Example:**
```tomo
>> (1.0).expm1()
= 1.7183

```
## Num.fdim

```tomo
Num.fdim : func(x: Num, y: Num -> Num)
```

Computes the positive difference between two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -

**Return:** The positive difference $\max(0, x - y)$.


**Example:**
```tomo
fd

>> (5.0).fdim(3)
= 2

```
## Num.floor

```tomo
Num.floor : func(x: Num -> Num)
```

Rounds a number down to the nearest integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to be rounded down.  | -

**Return:** The largest integer less than or equal to `x`.


**Example:**
```tomo
>> (3.7).floor()
= 3

```
## Num.hypot

```tomo
Num.hypot : func(x: Num, y: Num -> Num)
```

Computes the Euclidean norm, $\sqrt{x^2 + y^2}$, of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -

**Return:** The Euclidean norm of `x` and `y`.


**Example:**
```tomo
>> Num.hypot(3, 4)
= 5

```
## Num.is_between

```tomo
Num.is_between : func(x: Num, low: Num, high: Num -> Bool)
```

Determines if a number is between two numbers (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The integer to be checked.  | -
low | `Num` | The lower bound to check (inclusive).  | -
high | `Num` | The upper bound to check (inclusive).  | -

**Return:** `yes` if `low <= x and x <= high`, otherwise `no`


**Example:**
```tomo
>> (7.5).is_between(1, 10)
= yes
>> (7.5).is_between(100, 200)
= no
>> (7.5).is_between(1, 7.5)
= yes

```
## Num.isfinite

```tomo
Num.isfinite : func(n: Num -> Bool)
```

Checks if a number is finite.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to be checked.  | -

**Return:** `yes` if `n` is finite, `no` otherwise.


**Example:**
```tomo
>> (1.0).isfinite()
= yes
>> Num.INF.isfinite()
= no

```
## Num.isinf

```tomo
Num.isinf : func(n: Num -> Bool)
```

Checks if a number is infinite.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to be checked.  | -

**Return:** `yes` if `n` is infinite, `no` otherwise.


**Example:**
```tomo
>> Num.INF.isinf()
= yes
>> (1.0).isinf()
= no

```
## Num.j0

```tomo
Num.j0 : func(x: Num -> Num)
```

Computes the Bessel function of the first kind of order 0.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the first kind of order 0 of `x`.


**Example:**
```tomo
>> (0.0).j0()
= 1

```
## Num.j1

```tomo
Num.j1 : func(x: Num -> Num)
```

Computes the Bessel function of the first kind of order 1.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the first kind of order 1 of `x`.


**Example:**
```tomo
>> (0.0).j1()
= 0

```
## Num.log

```tomo
Num.log : func(x: Num -> Num)
```

Computes the natural logarithm (base $e$) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the natural logarithm is to be calculated.  | -

**Return:** The natural logarithm of `x`.


**Example:**
```tomo
>> Num.E.log()
= 1

```
## Num.log10

```tomo
Num.log10 : func(x: Num -> Num)
```

Computes the base-10 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the base-10 logarithm is to be calculated.  | -

**Return:** The base-10 logarithm of `x`.


**Example:**
```tomo
>> (100.0).log10()
= 2

```
## Num.log1p

```tomo
Num.log1p : func(x: Num -> Num)
```

Computes $\log(1 + x)$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which $\log(1 + x)$ is to be calculated.  | -

**Return:** The value of $\log(1 + x)$.


**Example:**
```tomo
>> (1.0).log1p()
= 0.6931

```
## Num.log2

```tomo
Num.log2 : func(x: Num -> Num)
```

Computes the base-2 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the base-2 logarithm is to be calculated.  | -

**Return:** The base-2 logarithm of `x`.


**Example:**
```tomo
>> (8.0).log2()
= 3

```
## Num.logb

```tomo
Num.logb : func(x: Num -> Num)
```

Computes the binary exponent (base-2 logarithm) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the binary exponent is to be calculated.  | -

**Return:** The binary exponent of `x`.


**Example:**
```tomo
>> (8.0).logb()
= 3

```
## Num.mix

```tomo
Num.mix : func(amount: Num, x: Num, y: Num -> Num)
```

Interpolates between two numbers based on a given amount.

Argument | Type | Description | Default
---------|------|-------------|---------
amount | `Num` | The interpolation factor (between `0` and `1`).  | -
x | `Num` | The starting number.  | -
y | `Num` | The ending number.  | -

**Return:** The interpolated number between `x` and `y` based on `amount`.


**Example:**
```tomo
>> (0.5).mix(10, 20)
= 15
>> (0.25).mix(10, 20)
= 12.5

```
## Num.near

```tomo
Num.near : func(x: Num, y: Num, ratio: Num = 1e-9, min_epsilon: Num = 1e-9 -> Bool)
```

Checks if two numbers are approximately equal within specified tolerances. If two numbers are within an absolute difference or the ratio between the two is small enough, they are considered near each other.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -
ratio | `Num` | The relative tolerance. Default is `1e-9`.  | `1e-9`
min_epsilon | `Num` | The absolute tolerance. Default is `1e-9`.  | `1e-9`

**Return:** `yes` if `x` and `y` are approximately equal within the specified tolerances, `no` otherwise.


**Example:**
```tomo
>> (1.0).near(1.000000001)
= yes

>> (100.0).near(110, ratio=0.1)
= yes

>> (5.0).near(5.1, min_epsilon=0.1)
= yes

```
## Num.nextafter

```tomo
Num.nextafter : func(x: Num, y: Num -> Num)
```

Computes the next representable value after a given number towards a specified direction.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The starting number.  | -
y | `Num` | The direction towards which to find the next representable value.  | -

**Return:** The next representable value after `x` in the direction of `y`.


**Example:**
```tomo
>> (1.0).nextafter(1.1)
= 1.0000000000000002

```
## Num.parse

```tomo
Num.parse : func(text: Text, remainder: &Text? = none -> Num?)
```

Converts a text representation of a number into a floating-point number.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text containing the number.  | -
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** The number represented by the text or `none` if the entire text can't be parsed as a number.


**Example:**
```tomo
>> Num.parse("3.14")
= 3.14 : Num?
>> Num.parse("1e3")
= 1000 : Num?

>> Num.parse("1.5junk")
= none : Num?
remainder : Text
>> Num.parse("1.5junk", &remainder)
= 1.5 : Num?
>> remainder
= "junk"

```
## Num.percent

```tomo
Num.percent : func(n: Num, precision: Num = 0.01 -> Text)
```

Convert a number into a percentage text with a percent sign.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to be converted to a percent.  | -
precision | `Num` | Round the percentage to this precision level.  | `0.01`

**Return:** A text representation of the number as a percentage with a percent sign.


**Example:**
```tomo
>> (0.5).percent()
= "50%"
>> (1./3.).percent(2)
= "33.33%"
>> (1./3.).percent(2, precision=0.0001)
= "33.3333%"
>> (1./3.).percent(2, precision=10.)
= "30%"

```
## Num.rint

```tomo
Num.rint : func(x: Num -> Num)
```

Rounds a number to the nearest integer, with ties rounded to the nearest even integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to be rounded.  | -

**Return:** The nearest integer value of `x`.


**Example:**
```tomo
>> (3.5).rint()
= 4
>> (2.5).rint()
= 2

```
## Num.round

```tomo
Num.round : func(x: Num -> Num)
```

Rounds a number to the nearest whole number integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to be rounded.  | -

**Return:** The nearest integer value of `x`.


**Example:**
```tomo
>> (2.3).round()
= 2
>> (2.7).round()
= 3

```
## Num.significand

```tomo
Num.significand : func(x: Num -> Num)
```

Extracts the significand (or mantissa) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number from which to extract the significand.  | -

**Return:** The significand of `x`.


**Example:**
```tomo
>> (1234.567).significand()
= 0.1234567

```
## Num.sin

```tomo
Num.sin : func(x: Num -> Num)
```

Computes the sine of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The angle in radians.  | -

**Return:** The sine of `x`.


**Example:**
```tomo
>> (0.0).sin()
= 0

```
## Num.sinh

```tomo
Num.sinh : func(x: Num -> Num)
```

Computes the hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the hyperbolic sine is to be calculated.  | -

**Return:** The hyperbolic sine of `x`.


**Example:**
```tomo
>> (0.0).sinh()
= 0

```
## Num.sqrt

```tomo
Num.sqrt : func(x: Num -> Num)
```

Computes the square root of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the square root is to be calculated.  | -

**Return:** The square root of `x`.


**Example:**
```tomo
>> (16.0).sqrt()
= 4

```
## Num.tan

```tomo
Num.tan : func(x: Num -> Num)
```

Computes the tangent of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The angle in radians.  | -

**Return:** The tangent of `x`.


**Example:**
```tomo
>> (0.0).tan()
= 0

```
## Num.tanh

```tomo
Num.tanh : func(x: Num -> Num)
```

Computes the hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the hyperbolic tangent is to be calculated.  | -

**Return:** The hyperbolic tangent of `x`.


**Example:**
```tomo
>> (0.0).tanh()
= 0

```
## Num.tgamma

```tomo
Num.tgamma : func(x: Num -> Num)
```

Computes the gamma function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the gamma function is to be calculated.  | -

**Return:** The gamma function of `x`.


**Example:**
```tomo
>> (1.0).tgamma()
= 1

```
## Num.trunc

```tomo
Num.trunc : func(x: Num -> Num)
```

Truncates a number to the nearest integer towards zero.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to be truncated.  | -

**Return:** The integer part of `x` towards zero.


**Example:**
```tomo
>> (3.7).trunc()
= 3
>> (-3.7).trunc()
= -3

```
## Num.with_precision

```tomo
Num.with_precision : func(n: Num, precision: Num -> Num)
```

Round a number to the given precision level (specified as `10`, `.1`, `.001` etc).

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to be rounded to a given precision.  | -
precision | `Num` | The precision to which the number should be rounded.  | -

**Return:** The number, rounded to the given precision level.


**Example:**
```tomo
>> (0.1234567).with_precision(0.01)
= 0.12
>> (123456.).with_precision(100)
= 123500
>> (1234567.).with_precision(5)
= 1234565

```
## Num.y0

```tomo
Num.y0 : func(x: Num -> Num)
```

Computes the Bessel function of the second kind of order 0.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the second kind of order 0 of `x`.


**Example:**
```tomo
>> (1.0).y0()
= -0.7652

```
## Num.y1

```tomo
Num.y1 : func(x: Num -> Num)
```

Computes the Bessel function of the second kind of order 1.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the second kind of order 1 of `x`.


**Example:**
```tomo
>> (1.0).y1()
= 0.4401

```
