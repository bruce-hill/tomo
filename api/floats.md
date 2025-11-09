% API

# Builtins

# Float64
## Float64.1_PI

```tomo
Float64.1_PI : Float64
```

The constant $\frac{1}{\pi}$.

## Float64.2_PI

```tomo
Float64.2_PI : Float64
```

The constant $2 \times \pi$.

## Float64.2_SQRTPI

```tomo
Float64.2_SQRTPI : Float64
```

The constant $2 \times \sqrt{\pi}$.

## Float64.E

```tomo
Float64.E : Float64
```

The base of the natural logarithm ($e$).

## Float64.INF

```tomo
Float64.INF : Float64
```

Positive infinity.

## Float64.LN10

```tomo
Float64.LN10 : Float64
```

The natural logarithm of 10.

## Float64.LN2

```tomo
Float64.LN2 : Float64
```

The natural logarithm of 2.

## Float64.LOG2E

```tomo
Float64.LOG2E : Float64
```

The base 2 logarithm of $e$

## Float64.PI

```tomo
Float64.PI : Float64
```

Pi ($\pi$).

## Float64.PI_2

```tomo
Float64.PI_2 : Float64
```

$\frac{\pi}{2}$

## Float64.PI_4

```tomo
Float64.PI_4 : Float64
```

$\frac{\pi}{4}$

## Float64.SQRT1_2

```tomo
Float64.SQRT1_2 : Float64
```

$\sqrt{\frac{1}{2}}$

## Float64.SQRT2

```tomo
Float64.SQRT2 : Float64
```

$\sqrt{2}$

## Float64.TAU

```tomo
Float64.TAU : Float64
```

Tau ($2 \times \pi$)

## Float64.abs

```tomo
Float64.abs : func(n: Float64 -> Float64)
```

Calculates the absolute value of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float64` | The number whose absolute value is to be computed.  | -

**Return:** The absolute value of `n`.


**Example:**
```tomo
assert (-3.5).abs() == 3.5

```
## Float64.acos

```tomo
Float64.acos : func(x: Float64 -> Float64)
```

Computes the arc cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the arc cosine is to be calculated.  | -

**Return:** The arc cosine of `x` in radians.


**Example:**
```tomo
assert (0.0).acos() == 1.5708

```
## Float64.acosh

```tomo
Float64.acosh : func(x: Float64 -> Float64)
```

Computes the inverse hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the inverse hyperbolic cosine is to be calculated.  | -

**Return:** The inverse hyperbolic cosine of `x`.


**Example:**
```tomo
assert (1.0).acosh() == 0

```
## Float64.asin

```tomo
Float64.asin : func(x: Float64 -> Float64)
```

Computes the arc sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the arc sine is to be calculated.  | -

**Return:** The arc sine of `x` in radians.


**Example:**
```tomo
assert (0.5).asin() == 0.5236

```
## Float64.asinh

```tomo
Float64.asinh : func(x: Float64 -> Float64)
```

Computes the inverse hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the inverse hyperbolic sine is to be calculated.  | -

**Return:** The inverse hyperbolic sine of `x`.


**Example:**
```tomo
assert (0.0).asinh() == 0

```
## Float64.atan

```tomo
Float64.atan : func(x: Float64 -> Float64)
```

Computes the arc tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the arc tangent is to be calculated.  | -

**Return:** The arc tangent of `x` in radians.


**Example:**
```tomo
assert (1.0).atan() == 0.7854

```
## Float64.atan2

```tomo
Float64.atan2 : func(x: Float64, y: Float64 -> Float64)
```

Computes the arc tangent of the quotient of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The numerator.  | -
y | `Float64` | The denominator.  | -

**Return:** The arc tangent of `x/y` in radians.


**Example:**
```tomo
assert Float64.atan2(1, 1) == 0.7854

```
## Float64.atanh

```tomo
Float64.atanh : func(x: Float64 -> Float64)
```

Computes the inverse hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the inverse hyperbolic tangent is to be calculated.  | -

**Return:** The inverse hyperbolic tangent of `x`.


**Example:**
```tomo
assert (0.5).atanh() == 0.5493

```
## Float64.cbrt

```tomo
Float64.cbrt : func(x: Float64 -> Float64)
```

Computes the cube root of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the cube root is to be calculated.  | -

**Return:** The cube root of `x`.


**Example:**
```tomo
assert (27.0).cbrt() == 3

```
## Float64.ceil

```tomo
Float64.ceil : func(x: Float64 -> Float64)
```

Rounds a number up to the nearest integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number to be rounded up.  | -

**Return:** The smallest integer greater than or equal to `x`.


**Example:**
```tomo
assert (3.2).ceil() == 4

```
## Float64.clamped

```tomo
Float64.clamped : func(x: Float64, low: Float64, high: Float64 -> Float64)
```

Returns the given number clamped between two values so that it is within that range.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number to clamp.  | -
low | `Float64` | The lowest value the result can take.  | -
high | `Float64` | The highest value the result can take.  | -

**Return:** The first argument clamped between the other two arguments.


**Example:**
```tomo
assert (2.5).clamped(5.5, 10.5) == 5.5

```
## Float64.copysign

```tomo
Float64.copysign : func(x: Float64, y: Float64 -> Float64)
```

Copies the sign of one number to another.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number whose magnitude will be copied.  | -
y | `Float64` | The number whose sign will be copied.  | -

**Return:** A number with the magnitude of `x` and the sign of `y`.


**Example:**
```tomo
assert (3.0).copysign(-1) == -3

```
## Float64.cos

```tomo
Float64.cos : func(x: Float64 -> Float64)
```

Computes the cosine of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The angle in radians.  | -

**Return:** The cosine of `x`.


**Example:**
```tomo
assert (0.0).cos() == 1

```
## Float64.cosh

```tomo
Float64.cosh : func(x: Float64 -> Float64)
```

Computes the hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the hyperbolic cosine is to be calculated.  | -

**Return:** The hyperbolic cosine of `x`.


**Example:**
```tomo
assert (0.0).cosh() == 1

```
## Float64.erf

```tomo
Float64.erf : func(x: Float64 -> Float64)
```

Computes the error function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the error function is to be calculated.  | -

**Return:** The error function of `x`.


**Example:**
```tomo
assert (0.0).erf() == 0

```
## Float64.erfc

```tomo
Float64.erfc : func(x: Float64 -> Float64)
```

Computes the complementary error function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the complementary error function is to be calculated.  | -

**Return:** The complementary error function of `x`.


**Example:**
```tomo
assert (0.0).erfc() == 1

```
## Float64.exp

```tomo
Float64.exp : func(x: Float64 -> Float64)
```

Computes the exponential function $e^x$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The exponent.  | -

**Return:** The value of $e^x$.


**Example:**
```tomo
assert (1.0).exp() == 2.7183

```
## Float64.exp2

```tomo
Float64.exp2 : func(x: Float64 -> Float64)
```

Computes $2^x$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The exponent.  | -

**Return:** The value of $2^x$.


**Example:**
```tomo
assert (3.0).exp2() == 8

```
## Float64.expm1

```tomo
Float64.expm1 : func(x: Float64 -> Float64)
```

Computes $e^x - 1$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The exponent.  | -

**Return:** The value of $e^x - 1$.


**Example:**
```tomo
assert (1.0).expm1() == 1.7183

```
## Float64.fdim

```tomo
Float64.fdim : func(x: Float64, y: Float64 -> Float64)
```

Computes the positive difference between two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The first number.  | -
y | `Float64` | The second number.  | -

**Return:** The positive difference $\max(0, x - y)$.


**Example:**
```tomo
fd

assert (5.0).fdim(3) == 2

```
## Float64.floor

```tomo
Float64.floor : func(x: Float64 -> Float64)
```

Rounds a number down to the nearest integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number to be rounded down.  | -

**Return:** The largest integer less than or equal to `x`.


**Example:**
```tomo
assert (3.7).floor() == 3

```
## Float64.hypot

```tomo
Float64.hypot : func(x: Float64, y: Float64 -> Float64)
```

Computes the Euclidean norm, $\sqrt{x^2 + y^2}$, of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The first number.  | -
y | `Float64` | The second number.  | -

**Return:** The Euclidean norm of `x` and `y`.


**Example:**
```tomo
assert Float64.hypot(3, 4) == 5

```
## Float64.is_between

```tomo
Float64.is_between : func(x: Float64, low: Float64, high: Float64 -> Bool)
```

Determines if a number is between two numbers (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The integer to be checked.  | -
low | `Float64` | The lower bound to check (inclusive).  | -
high | `Float64` | The upper bound to check (inclusive).  | -

**Return:** `yes` if `low <= x and x <= high`, otherwise `no`


**Example:**
```tomo
assert (7.5).is_between(1, 10) == yes
assert (7.5).is_between(100, 200) == no
assert (7.5).is_between(1, 7.5) == yes

```
## Float64.isfinite

```tomo
Float64.isfinite : func(n: Float64 -> Bool)
```

Checks if a number is finite.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float64` | The number to be checked.  | -

**Return:** `yes` if `n` is finite, `no` otherwise.


**Example:**
```tomo
assert (1.0).isfinite() == yes
assert Float64.INF.isfinite() == no

```
## Float64.isinf

```tomo
Float64.isinf : func(n: Float64 -> Bool)
```

Checks if a number is infinite.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float64` | The number to be checked.  | -

**Return:** `yes` if `n` is infinite, `no` otherwise.


**Example:**
```tomo
assert Float64.INF.isinf() == yes
assert (1.0).isinf() == no

```
## Float64.j0

```tomo
Float64.j0 : func(x: Float64 -> Float64)
```

Computes the Bessel function of the first kind of order 0.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the first kind of order 0 of `x`.


**Example:**
```tomo
assert (0.0).j0() == 1

```
## Float64.j1

```tomo
Float64.j1 : func(x: Float64 -> Float64)
```

Computes the Bessel function of the first kind of order 1.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the first kind of order 1 of `x`.


**Example:**
```tomo
assert (0.0).j1() == 0

```
## Float64.log

```tomo
Float64.log : func(x: Float64 -> Float64)
```

Computes the natural logarithm (base $e$) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the natural logarithm is to be calculated.  | -

**Return:** The natural logarithm of `x`.


**Example:**
```tomo
assert Float64.E.log() == 1

```
## Float64.log10

```tomo
Float64.log10 : func(x: Float64 -> Float64)
```

Computes the base-10 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the base-10 logarithm is to be calculated.  | -

**Return:** The base-10 logarithm of `x`.


**Example:**
```tomo
assert (100.0).log10() == 2

```
## Float64.log1p

```tomo
Float64.log1p : func(x: Float64 -> Float64)
```

Computes $\log(1 + x)$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which $\log(1 + x)$ is to be calculated.  | -

**Return:** The value of $\log(1 + x)$.


**Example:**
```tomo
assert (1.0).log1p() == 0.6931

```
## Float64.log2

```tomo
Float64.log2 : func(x: Float64 -> Float64)
```

Computes the base-2 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the base-2 logarithm is to be calculated.  | -

**Return:** The base-2 logarithm of `x`.


**Example:**
```tomo
assert (8.0).log2() == 3

```
## Float64.logb

```tomo
Float64.logb : func(x: Float64 -> Float64)
```

Computes the binary exponent (base-2 logarithm) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the binary exponent is to be calculated.  | -

**Return:** The binary exponent of `x`.


**Example:**
```tomo
assert (8.0).logb() == 3

```
## Float64.mix

```tomo
Float64.mix : func(amount: Float64, x: Float64, y: Float64 -> Float64)
```

Interpolates between two numbers based on a given amount.

Argument | Type | Description | Default
---------|------|-------------|---------
amount | `Float64` | The interpolation factor (between `0` and `1`).  | -
x | `Float64` | The starting number.  | -
y | `Float64` | The ending number.  | -

**Return:** The interpolated number between `x` and `y` based on `amount`.


**Example:**
```tomo
assert (0.5).mix(10, 20) == 15
assert (0.25).mix(10, 20) == 12.5

```
## Float64.near

```tomo
Float64.near : func(x: Float64, y: Float64, ratio: Float64 = 1e-9, min_epsilon: Float64 = 1e-9 -> Bool)
```

Checks if two numbers are approximately equal within specified tolerances. If two numbers are within an absolute difference or the ratio between the two is small enough, they are considered near each other.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The first number.  | -
y | `Float64` | The second number.  | -
ratio | `Float64` | The relative tolerance. Default is `1e-9`.  | `1e-9`
min_epsilon | `Float64` | The absolute tolerance. Default is `1e-9`.  | `1e-9`

**Return:** `yes` if `x` and `y` are approximately equal within the specified tolerances, `no` otherwise.


**Example:**
```tomo
assert (1.0).near(1.000000001) == yes
assert (100.0).near(110, ratio=0.1) == yes
assert (5.0).near(5.1, min_epsilon=0.1) == yes

```
## Float64.nextafter

```tomo
Float64.nextafter : func(x: Float64, y: Float64 -> Float64)
```

Computes the next representable value after a given number towards a specified direction.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The starting number.  | -
y | `Float64` | The direction towards which to find the next representable value.  | -

**Return:** The next representable value after `x` in the direction of `y`.


**Example:**
```tomo
assert (1.0).nextafter(1.1) == 1.0000000000000002

```
## Float64.parse

```tomo
Float64.parse : func(text: Text, remainder: &Text? = none -> Float64?)
```

Converts a text representation of a number into a floating-point number.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text containing the number.  | -
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** The number represented by the text or `none` if the entire text can't be parsed as a number.


**Example:**
```tomo
assert Float64.parse("3.14") == 3.14
assert Float64.parse("1e3") == 1000
assert Float64.parse("1.5junk") == none
remainder : Text
assert Float64.parse("1.5junk", &remainder) == 1.5
assert remainder == "junk"

```
## Float64.percent

```tomo
Float64.percent : func(n: Float64, precision: Float64 = 0.01 -> Text)
```

Convert a number into a percentage text with a percent sign.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float64` | The number to be converted to a percent.  | -
precision | `Float64` | Round the percentage to this precision level.  | `0.01`

**Return:** A text representation of the number as a percentage with a percent sign.


**Example:**
```tomo
assert (0.5).percent() == "50%"
assert (1./3.).percent(2) == "33.33%"
assert (1./3.).percent(2, precision=0.0001) == "33.3333%"
assert (1./3.).percent(2, precision=10.) == "30%"

```
## Float64.rint

```tomo
Float64.rint : func(x: Float64 -> Float64)
```

Rounds a number to the nearest integer, with ties rounded to the nearest even integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number to be rounded.  | -

**Return:** The nearest integer value of `x`.


**Example:**
```tomo
assert (3.5).rint() == 4
assert (2.5).rint() == 2

```
## Float64.round

```tomo
Float64.round : func(x: Float64 -> Float64)
```

Rounds a number to the nearest whole number integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number to be rounded.  | -

**Return:** The nearest integer value of `x`.


**Example:**
```tomo
assert (2.3).round() == 2
assert (2.7).round() == 3

```
## Float64.significand

```tomo
Float64.significand : func(x: Float64 -> Float64)
```

Extracts the significand (or mantissa) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number from which to extract the significand.  | -

**Return:** The significand of `x`.


**Example:**
```tomo
assert (1234.567).significand() == 0.1234567

```
## Float64.sin

```tomo
Float64.sin : func(x: Float64 -> Float64)
```

Computes the sine of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The angle in radians.  | -

**Return:** The sine of `x`.


**Example:**
```tomo
assert (0.0).sin() == 0

```
## Float64.sinh

```tomo
Float64.sinh : func(x: Float64 -> Float64)
```

Computes the hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the hyperbolic sine is to be calculated.  | -

**Return:** The hyperbolic sine of `x`.


**Example:**
```tomo
assert (0.0).sinh() == 0

```
## Float64.sqrt

```tomo
Float64.sqrt : func(x: Float64 -> Float64)
```

Computes the square root of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the square root is to be calculated.  | -

**Return:** The square root of `x`.


**Example:**
```tomo
assert (16.0).sqrt() == 4

```
## Float64.tan

```tomo
Float64.tan : func(x: Float64 -> Float64)
```

Computes the tangent of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The angle in radians.  | -

**Return:** The tangent of `x`.


**Example:**
```tomo
assert (0.0).tan() == 0

```
## Float64.tanh

```tomo
Float64.tanh : func(x: Float64 -> Float64)
```

Computes the hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the hyperbolic tangent is to be calculated.  | -

**Return:** The hyperbolic tangent of `x`.


**Example:**
```tomo
assert (0.0).tanh() == 0

```
## Float64.tgamma

```tomo
Float64.tgamma : func(x: Float64 -> Float64)
```

Computes the gamma function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the gamma function is to be calculated.  | -

**Return:** The gamma function of `x`.


**Example:**
```tomo
assert (1.0).tgamma() == 1

```
## Float64.trunc

```tomo
Float64.trunc : func(x: Float64 -> Float64)
```

Truncates a number to the nearest integer towards zero.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number to be truncated.  | -

**Return:** The integer part of `x` towards zero.


**Example:**
```tomo
assert (3.7).trunc() == 3
assert (-3.7).trunc() == -3

```
## Float64.with_precision

```tomo
Float64.with_precision : func(n: Float64, precision: Float64 -> Float64)
```

Round a number to the given precision level (specified as `10`, `.1`, `.001` etc).

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float64` | The number to be rounded to a given precision.  | -
precision | `Float64` | The precision to which the number should be rounded.  | -

**Return:** The number, rounded to the given precision level.


**Example:**
```tomo
assert (0.1234567).with_precision(0.01) == 0.12
assert (123456.).with_precision(100) == 123500
assert (1234567.).with_precision(5) == 1234565

```
## Float64.y0

```tomo
Float64.y0 : func(x: Float64 -> Float64)
```

Computes the Bessel function of the second kind of order 0.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the second kind of order 0 of `x`.


**Example:**
```tomo
assert (1.0).y0() == -0.7652

```
## Float64.y1

```tomo
Float64.y1 : func(x: Float64 -> Float64)
```

Computes the Bessel function of the second kind of order 1.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float64` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the second kind of order 1 of `x`.


**Example:**
```tomo
assert (1.0).y1() == 0.4401

```
