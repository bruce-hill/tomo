% API

# Builtins

# Float
## Float.1_PI

```tomo
Float.1_PI : Float
```

The constant $\frac{1}{\pi}$.

## Float.2_PI

```tomo
Float.2_PI : Float
```

The constant $2 \times \pi$.

## Float.2_SQRTPI

```tomo
Float.2_SQRTPI : Float
```

The constant $2 \times \sqrt{\pi}$.

## Float.E

```tomo
Float.E : Float
```

The base of the natural logarithm ($e$).

## Float.INF

```tomo
Float.INF : Float
```

Positive infinity.

## Float.LN10

```tomo
Float.LN10 : Float
```

The natural logarithm of 10.

## Float.LN2

```tomo
Float.LN2 : Float
```

The natural logarithm of 2.

## Float.LOG2E

```tomo
Float.LOG2E : Float
```

The base 2 logarithm of $e$

## Float.PI

```tomo
Float.PI : Float
```

Pi ($\pi$).

## Float.PI_2

```tomo
Float.PI_2 : Float
```

$\frac{\pi}{2}$

## Float.PI_4

```tomo
Float.PI_4 : Float
```

$\frac{\pi}{4}$

## Float.SQRT1_2

```tomo
Float.SQRT1_2 : Float
```

$\sqrt{\frac{1}{2}}$

## Float.SQRT2

```tomo
Float.SQRT2 : Float
```

$\sqrt{2}$

## Float.TAU

```tomo
Float.TAU : Float
```

Tau ($2 \times \pi$)

## Float.abs

```tomo
Float.abs : func(n: Float -> Float)
```

Calculates the absolute value of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float` | The number whose absolute value is to be computed.  | -

**Return:** The absolute value of `n`.


**Example:**
```tomo
assert (-3.5).abs() == 3.5

```
## Float.acos

```tomo
Float.acos : func(x: Float -> Float)
```

Computes the arc cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the arc cosine is to be calculated.  | -

**Return:** The arc cosine of `x` in radians.


**Example:**
```tomo
assert (0.0).acos() == 1.5708

```
## Float.acosh

```tomo
Float.acosh : func(x: Float -> Float)
```

Computes the inverse hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the inverse hyperbolic cosine is to be calculated.  | -

**Return:** The inverse hyperbolic cosine of `x`.


**Example:**
```tomo
assert (1.0).acosh() == 0

```
## Float.asin

```tomo
Float.asin : func(x: Float -> Float)
```

Computes the arc sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the arc sine is to be calculated.  | -

**Return:** The arc sine of `x` in radians.


**Example:**
```tomo
assert (0.5).asin() == 0.5236

```
## Float.asinh

```tomo
Float.asinh : func(x: Float -> Float)
```

Computes the inverse hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the inverse hyperbolic sine is to be calculated.  | -

**Return:** The inverse hyperbolic sine of `x`.


**Example:**
```tomo
assert (0.0).asinh() == 0

```
## Float.atan

```tomo
Float.atan : func(x: Float -> Float)
```

Computes the arc tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the arc tangent is to be calculated.  | -

**Return:** The arc tangent of `x` in radians.


**Example:**
```tomo
assert (1.0).atan() == 0.7854

```
## Float.atan2

```tomo
Float.atan2 : func(x: Float, y: Float -> Float)
```

Computes the arc tangent of the quotient of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The numerator.  | -
y | `Float` | The denominator.  | -

**Return:** The arc tangent of `x/y` in radians.


**Example:**
```tomo
assert Float.atan2(1, 1) == 0.7854

```
## Float.atanh

```tomo
Float.atanh : func(x: Float -> Float)
```

Computes the inverse hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the inverse hyperbolic tangent is to be calculated.  | -

**Return:** The inverse hyperbolic tangent of `x`.


**Example:**
```tomo
assert (0.5).atanh() == 0.5493

```
## Float.cbrt

```tomo
Float.cbrt : func(x: Float -> Float)
```

Computes the cube root of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the cube root is to be calculated.  | -

**Return:** The cube root of `x`.


**Example:**
```tomo
assert (27.0).cbrt() == 3

```
## Float.ceil

```tomo
Float.ceil : func(x: Float -> Float)
```

Rounds a number up to the nearest integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number to be rounded up.  | -

**Return:** The smallest integer greater than or equal to `x`.


**Example:**
```tomo
assert (3.2).ceil() == 4

```
## Float.clamped

```tomo
Float.clamped : func(x: Float, low: Float, high: Float -> Float)
```

Returns the given number clamped between two values so that it is within that range.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number to clamp.  | -
low | `Float` | The lowest value the result can take.  | -
high | `Float` | The highest value the result can take.  | -

**Return:** The first argument clamped between the other two arguments.


**Example:**
```tomo
assert (2.5).clamped(5.5, 10.5) == 5.5

```
## Float.copysign

```tomo
Float.copysign : func(x: Float, y: Float -> Float)
```

Copies the sign of one number to another.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number whose magnitude will be copied.  | -
y | `Float` | The number whose sign will be copied.  | -

**Return:** A number with the magnitude of `x` and the sign of `y`.


**Example:**
```tomo
assert (3.0).copysign(-1) == -3

```
## Float.cos

```tomo
Float.cos : func(x: Float -> Float)
```

Computes the cosine of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The angle in radians.  | -

**Return:** The cosine of `x`.


**Example:**
```tomo
assert (0.0).cos() == 1

```
## Float.cosh

```tomo
Float.cosh : func(x: Float -> Float)
```

Computes the hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the hyperbolic cosine is to be calculated.  | -

**Return:** The hyperbolic cosine of `x`.


**Example:**
```tomo
assert (0.0).cosh() == 1

```
## Float.erf

```tomo
Float.erf : func(x: Float -> Float)
```

Computes the error function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the error function is to be calculated.  | -

**Return:** The error function of `x`.


**Example:**
```tomo
assert (0.0).erf() == 0

```
## Float.erfc

```tomo
Float.erfc : func(x: Float -> Float)
```

Computes the complementary error function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the complementary error function is to be calculated.  | -

**Return:** The complementary error function of `x`.


**Example:**
```tomo
assert (0.0).erfc() == 1

```
## Float.exp

```tomo
Float.exp : func(x: Float -> Float)
```

Computes the exponential function $e^x$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The exponent.  | -

**Return:** The value of $e^x$.


**Example:**
```tomo
assert (1.0).exp() == 2.7183

```
## Float.exp2

```tomo
Float.exp2 : func(x: Float -> Float)
```

Computes $2^x$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The exponent.  | -

**Return:** The value of $2^x$.


**Example:**
```tomo
assert (3.0).exp2() == 8

```
## Float.expm1

```tomo
Float.expm1 : func(x: Float -> Float)
```

Computes $e^x - 1$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The exponent.  | -

**Return:** The value of $e^x - 1$.


**Example:**
```tomo
assert (1.0).expm1() == 1.7183

```
## Float.fdim

```tomo
Float.fdim : func(x: Float, y: Float -> Float)
```

Computes the positive difference between two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The first number.  | -
y | `Float` | The second number.  | -

**Return:** The positive difference $\max(0, x - y)$.


**Example:**
```tomo
fd

assert (5.0).fdim(3) == 2

```
## Float.floor

```tomo
Float.floor : func(x: Float -> Float)
```

Rounds a number down to the nearest integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number to be rounded down.  | -

**Return:** The largest integer less than or equal to `x`.


**Example:**
```tomo
assert (3.7).floor() == 3

```
## Float.hypot

```tomo
Float.hypot : func(x: Float, y: Float -> Float)
```

Computes the Euclidean norm, $\sqrt{x^2 + y^2}$, of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The first number.  | -
y | `Float` | The second number.  | -

**Return:** The Euclidean norm of `x` and `y`.


**Example:**
```tomo
assert Float.hypot(3, 4) == 5

```
## Float.is_between

```tomo
Float.is_between : func(x: Float, low: Float, high: Float -> Bool)
```

Determines if a number is between two numbers (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The integer to be checked.  | -
low | `Float` | The lower bound to check (inclusive).  | -
high | `Float` | The upper bound to check (inclusive).  | -

**Return:** `yes` if `low <= x and x <= high`, otherwise `no`


**Example:**
```tomo
assert (7.5).is_between(1, 10) == yes
assert (7.5).is_between(100, 200) == no
assert (7.5).is_between(1, 7.5) == yes

```
## Float.isfinite

```tomo
Float.isfinite : func(n: Float -> Bool)
```

Checks if a number is finite.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float` | The number to be checked.  | -

**Return:** `yes` if `n` is finite, `no` otherwise.


**Example:**
```tomo
assert (1.0).isfinite() == yes
assert Float.INF.isfinite() == no

```
## Float.isinf

```tomo
Float.isinf : func(n: Float -> Bool)
```

Checks if a number is infinite.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float` | The number to be checked.  | -

**Return:** `yes` if `n` is infinite, `no` otherwise.


**Example:**
```tomo
assert Float.INF.isinf() == yes
assert (1.0).isinf() == no

```
## Float.j0

```tomo
Float.j0 : func(x: Float -> Float)
```

Computes the Bessel function of the first kind of order 0.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the first kind of order 0 of `x`.


**Example:**
```tomo
assert (0.0).j0() == 1

```
## Float.j1

```tomo
Float.j1 : func(x: Float -> Float)
```

Computes the Bessel function of the first kind of order 1.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the first kind of order 1 of `x`.


**Example:**
```tomo
assert (0.0).j1() == 0

```
## Float.log

```tomo
Float.log : func(x: Float -> Float)
```

Computes the natural logarithm (base $e$) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the natural logarithm is to be calculated.  | -

**Return:** The natural logarithm of `x`.


**Example:**
```tomo
assert Float.E.log() == 1

```
## Float.log10

```tomo
Float.log10 : func(x: Float -> Float)
```

Computes the base-10 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the base-10 logarithm is to be calculated.  | -

**Return:** The base-10 logarithm of `x`.


**Example:**
```tomo
assert (100.0).log10() == 2

```
## Float.log1p

```tomo
Float.log1p : func(x: Float -> Float)
```

Computes $\log(1 + x)$ for a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which $\log(1 + x)$ is to be calculated.  | -

**Return:** The value of $\log(1 + x)$.


**Example:**
```tomo
assert (1.0).log1p() == 0.6931

```
## Float.log2

```tomo
Float.log2 : func(x: Float -> Float)
```

Computes the base-2 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the base-2 logarithm is to be calculated.  | -

**Return:** The base-2 logarithm of `x`.


**Example:**
```tomo
assert (8.0).log2() == 3

```
## Float.logb

```tomo
Float.logb : func(x: Float -> Float)
```

Computes the binary exponent (base-2 logarithm) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the binary exponent is to be calculated.  | -

**Return:** The binary exponent of `x`.


**Example:**
```tomo
assert (8.0).logb() == 3

```
## Float.mix

```tomo
Float.mix : func(amount: Float, x: Float, y: Float -> Float)
```

Interpolates between two numbers based on a given amount.

Argument | Type | Description | Default
---------|------|-------------|---------
amount | `Float` | The interpolation factor (between `0` and `1`).  | -
x | `Float` | The starting number.  | -
y | `Float` | The ending number.  | -

**Return:** The interpolated number between `x` and `y` based on `amount`.


**Example:**
```tomo
assert (0.5).mix(10, 20) == 15
assert (0.25).mix(10, 20) == 12.5

```
## Float.near

```tomo
Float.near : func(x: Float, y: Float, ratio: Float = 1e-9, min_epsilon: Float = 1e-9 -> Bool)
```

Checks if two numbers are approximately equal within specified tolerances. If two numbers are within an absolute difference or the ratio between the two is small enough, they are considered near each other.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The first number.  | -
y | `Float` | The second number.  | -
ratio | `Float` | The relative tolerance. Default is `1e-9`.  | `1e-9`
min_epsilon | `Float` | The absolute tolerance. Default is `1e-9`.  | `1e-9`

**Return:** `yes` if `x` and `y` are approximately equal within the specified tolerances, `no` otherwise.


**Example:**
```tomo
assert (1.0).near(1.000000001) == yes
assert (100.0).near(110, ratio=0.1) == yes
assert (5.0).near(5.1, min_epsilon=0.1) == yes

```
## Float.nextafter

```tomo
Float.nextafter : func(x: Float, y: Float -> Float)
```

Computes the next representable value after a given number towards a specified direction.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The starting number.  | -
y | `Float` | The direction towards which to find the next representable value.  | -

**Return:** The next representable value after `x` in the direction of `y`.


**Example:**
```tomo
assert (1.0).nextafter(1.1) == 1.0000000000000002

```
## Float.parse

```tomo
Float.parse : func(text: Text, remainder: &Text? = none -> Float?)
```

Converts a text representation of a number into a floating-point number.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text containing the number.  | -
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** The number represented by the text or `none` if the entire text can't be parsed as a number.


**Example:**
```tomo
assert Float.parse("3.14") == 3.14
assert Float.parse("1e3") == 1000
assert Float.parse("1.5junk") == none
remainder : Text
assert Float.parse("1.5junk", &remainder) == 1.5
assert remainder == "junk"

```
## Float.percent

```tomo
Float.percent : func(n: Float, precision: Float = 0.01 -> Text)
```

Convert a number into a percentage text with a percent sign.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float` | The number to be converted to a percent.  | -
precision | `Float` | Round the percentage to this precision level.  | `0.01`

**Return:** A text representation of the number as a percentage with a percent sign.


**Example:**
```tomo
assert (0.5).percent() == "50%"
assert (1./3.).percent(2) == "33.33%"
assert (1./3.).percent(2, precision=0.0001) == "33.3333%"
assert (1./3.).percent(2, precision=10.) == "30%"

```
## Float.rint

```tomo
Float.rint : func(x: Float -> Float)
```

Rounds a number to the nearest integer, with ties rounded to the nearest even integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number to be rounded.  | -

**Return:** The nearest integer value of `x`.


**Example:**
```tomo
assert (3.5).rint() == 4
assert (2.5).rint() == 2

```
## Float.round

```tomo
Float.round : func(x: Float -> Float)
```

Rounds a number to the nearest whole number integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number to be rounded.  | -

**Return:** The nearest integer value of `x`.


**Example:**
```tomo
assert (2.3).round() == 2
assert (2.7).round() == 3

```
## Float.significand

```tomo
Float.significand : func(x: Float -> Float)
```

Extracts the significand (or mantissa) of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number from which to extract the significand.  | -

**Return:** The significand of `x`.


**Example:**
```tomo
assert (1234.567).significand() == 0.1234567

```
## Float.sin

```tomo
Float.sin : func(x: Float -> Float)
```

Computes the sine of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The angle in radians.  | -

**Return:** The sine of `x`.


**Example:**
```tomo
assert (0.0).sin() == 0

```
## Float.sinh

```tomo
Float.sinh : func(x: Float -> Float)
```

Computes the hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the hyperbolic sine is to be calculated.  | -

**Return:** The hyperbolic sine of `x`.


**Example:**
```tomo
assert (0.0).sinh() == 0

```
## Float.sqrt

```tomo
Float.sqrt : func(x: Float -> Float)
```

Computes the square root of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the square root is to be calculated.  | -

**Return:** The square root of `x`.


**Example:**
```tomo
assert (16.0).sqrt() == 4

```
## Float.tan

```tomo
Float.tan : func(x: Float -> Float)
```

Computes the tangent of a number (angle in radians).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The angle in radians.  | -

**Return:** The tangent of `x`.


**Example:**
```tomo
assert (0.0).tan() == 0

```
## Float.tanh

```tomo
Float.tanh : func(x: Float -> Float)
```

Computes the hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the hyperbolic tangent is to be calculated.  | -

**Return:** The hyperbolic tangent of `x`.


**Example:**
```tomo
assert (0.0).tanh() == 0

```
## Float.tgamma

```tomo
Float.tgamma : func(x: Float -> Float)
```

Computes the gamma function of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the gamma function is to be calculated.  | -

**Return:** The gamma function of `x`.


**Example:**
```tomo
assert (1.0).tgamma() == 1

```
## Float.trunc

```tomo
Float.trunc : func(x: Float -> Float)
```

Truncates a number to the nearest integer towards zero.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number to be truncated.  | -

**Return:** The integer part of `x` towards zero.


**Example:**
```tomo
assert (3.7).trunc() == 3
assert (-3.7).trunc() == -3

```
## Float.with_precision

```tomo
Float.with_precision : func(n: Float, precision: Float -> Float)
```

Round a number to the given precision level (specified as `10`, `.1`, `.001` etc).

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Float` | The number to be rounded to a given precision.  | -
precision | `Float` | The precision to which the number should be rounded.  | -

**Return:** The number, rounded to the given precision level.


**Example:**
```tomo
assert (0.1234567).with_precision(0.01) == 0.12
assert (123456.).with_precision(100) == 123500
assert (1234567.).with_precision(5) == 1234565

```
## Float.y0

```tomo
Float.y0 : func(x: Float -> Float)
```

Computes the Bessel function of the second kind of order 0.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the second kind of order 0 of `x`.


**Example:**
```tomo
assert (1.0).y0() == -0.7652

```
## Float.y1

```tomo
Float.y1 : func(x: Float -> Float)
```

Computes the Bessel function of the second kind of order 1.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Float` | The number for which the Bessel function is to be calculated.  | -

**Return:** The Bessel function of the second kind of order 1 of `x`.


**Example:**
```tomo
assert (1.0).y1() == 0.4401

```
