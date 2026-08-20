% API

# Builtins

# Num
## Num.PI

```tomo
Num.PI : Num
```

The ratio of a circle's circumference to its diameter, held exactly rather than as an approximation.


**Example:**
```tomo
assert Num.PI.sin() == 0
assert Num.PI.digits(10) == "3.1415926535…"

```
## Num.TAU

```tomo
Num.TAU : Num
```

Two times pi, held exactly.


**Example:**
```tomo
assert Num.TAU == 2 * Num.PI

```
## Num.abs

```tomo
Num.abs : func(x: Num -> Num)
```

The absolute value of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose absolute value is to be computed.  | -

**Return:** The absolute value of `x`.


**Example:**
```tomo
assert (-3.5).abs() == 3.5

```
## Num.acos

```tomo
Num.acos : func(x: Num -> Num?)
```

The arc cosine of a number, in radians.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose arc cosine is to be computed.  | -

**Return:** The arc cosine of `x`, or `none` if `x` is outside [-1, 1].


**Example:**
```tomo
assert (1.).acos()! == 0
assert (2.).acos() == none

```
## Num.asin

```tomo
Num.asin : func(x: Num -> Num?)
```

The arc sine of a number, in radians.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose arc sine is to be computed.  | -

**Return:** The arc sine of `x`, or `none` if `x` is outside [-1, 1].


**Example:**
```tomo
assert (0.).asin()! == 0
assert (2.).asin() == none

```
## Num.atan

```tomo
Num.atan : func(x: Num -> Num)
```

The arc tangent of a number, in radians. Defined for every number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose arc tangent is to be computed.  | -

**Return:** The arc tangent of `x`.


**Example:**
```tomo
assert (0.).atan() == 0
assert (1.).atan() == Num.PI / 4

```
## Num.atan2

```tomo
Num.atan2 : func(y: Num, x: Num -> Num?)
```

The angle of the point `(x, y)`, in radians, placed in the correct quadrant by the signs of both arguments.

Argument | Type | Description | Default
---------|------|-------------|---------
y | `Num` | The y coordinate.  | -
x | `Num` | The x coordinate.  | -

**Return:** The angle in (-pi, pi], or `none` at the origin, where no angle is defined.


**Example:**
```tomo
assert (1.).atan2(1)! == Num.PI / 4
assert (0.).atan2(0) == none

```
## Num.ceil

```tomo
Num.ceil : func(x: Num -> Num)
```

The smallest whole number greater than or equal to a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to round up.  | -

**Return:** `x` rounded up.


**Example:**
```tomo
assert (4.2).ceil() == 5
assert (-4.2).ceil() == -4

```
## Num.clamped

```tomo
Num.clamped : func(x: Num, low: Num, high: Num -> Num)
```

A number restricted to a range.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to clamp.  | -
low | `Num` | The lowest allowed value.  | -
high | `Num` | The highest allowed value.  | -

**Return:** `x`, or the nearer of `low` and `high` if `x` falls outside them.


**Example:**
```tomo
assert (1.5).clamped(0, 1) == 1
assert (0.5).clamped(0, 1) == 0.5

```
## Num.cos

```tomo
Num.cos : func(x: Num -> Num)
```

The cosine of an angle in radians.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The angle in radians.  | -

**Return:** The cosine of `x`.


**Example:**
```tomo
assert (0.).cos() == 1
assert Num.PI.cos() == -1

```
## Num.cosh

```tomo
Num.cosh : func(x: Num -> Num)
```

The hyperbolic cosine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose hyperbolic cosine is to be computed.  | -

**Return:** The hyperbolic cosine of `x`.


**Example:**
```tomo
assert (0.).cosh() == 1

```
## Num.digits

```tomo
Num.digits : func(n: Num, digits: Int = 15, ellipsis: Text = "…" -> Text)
```

The decimal expansion of a number, to at most the requested number of fractional digits. A value that fits exactly stops early rather than padding with zeros, and gets no marker. A value that doesn't shows a truncated prefix of its true expansion with the ellipsis appended -- never a rounding: every digit shown is a digit the value actually has. To round, round first.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to expand.  | -
digits | `Int` | The maximum number of fractional digits.  | `15`
ellipsis | `Text` | Appended when the digits don't capture the value exactly.  | `"…"`

**Return:** The decimal expansion as text.


**Example:**
```tomo
assert (1/3).digits(10) == "0.3333333333…"
assert Num.PI.digits(10) == "3.1415926535…"
assert (2/3).digits(10, ellipsis="") == "0.6666666666"
assert (0.25).digits(10) == "0.25"

```
## Num.exp

```tomo
Num.exp : func(x: Num -> Num)
```

e raised to the given power.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The exponent.  | -

**Return:** e to the power of `x`.


**Example:**
```tomo
assert (0.).exp() == 1

```
## Num.floor

```tomo
Num.floor : func(x: Num -> Num)
```

The largest whole number less than or equal to a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to round down.  | -

**Return:** `x` rounded down.


**Example:**
```tomo
assert (4.8).floor() == 4
assert (-4.2).floor() == -5

```
## Num.gcd

```tomo
Num.gcd : func(x: Num, y: Num -> Num?)
```

The greatest common divisor of two numbers, generalized to fractions: the largest value by which both can be divided to give whole numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -

**Return:** The greatest common divisor, or `none` if either argument is irrational and so has no such divisor.


**Example:**
```tomo
assert (12.).gcd(18)! == 6
assert (1./2.).gcd(1./3.)! == 1./6.

```
## Num.inverse

```tomo
Num.inverse : func(x: Num -> Num?)
```

One divided by a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to invert.  | -

**Return:** The reciprocal of `x`, or `none` if `x` is zero.


**Example:**
```tomo
assert (4.).inverse()! == 0.25
assert (0.).inverse() == none

```
## Num.is_between

```tomo
Num.is_between : func(x: Num, low: Num, high: Num -> Bool)
```

Whether a number lies within a range, inclusive.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to check.  | -
low | `Num` | The lower bound.  | -
high | `Num` | The upper bound.  | -

**Return:** `yes` if `low <= x <= high`.


**Example:**
```tomo
assert (0.5).is_between(0, 1)
assert not (1.5).is_between(0, 1)

```
## Num.is_exact

```tomo
Num.is_exact : func(n: Num, digits: Int = 15 -> Bool)
```

Whether the requested number of fractional digits represents a number exactly, rather than rounding it.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to check.  | -
digits | `Int` | The number of fractional digits.  | `15`

**Return:** `yes` if `:digits(digits)` is the value itself.


**Example:**
```tomo
assert (0.25).is_exact(10)
assert not (1./3.).is_exact(10)

```
## Num.is_integer

```tomo
Num.is_integer : func(n: Num -> Bool)
```

Whether a number is a whole number.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to check.  | -

**Return:** `yes` if `n` has no fractional part.


**Example:**
```tomo
assert (4.).is_integer()
assert not (4.5).is_integer()

```
## Num.is_rational

```tomo
Num.is_rational : func(n: Num -> Bool)
```

Whether a number can be written exactly as a fraction, as opposed to an irrational value like the square root of two.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to check.  | -

**Return:** `yes` if `n` is rational.


**Example:**
```tomo
assert (1./3.).is_rational()
assert not (2.).sqrt()!.is_rational()

```
## Num.lcm

```tomo
Num.lcm : func(x: Num, y: Num -> Num?)
```

The least common multiple of two numbers, generalized to fractions.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -

**Return:** The least common multiple, or `none` if either argument is irrational.


**Example:**
```tomo
assert (4.).lcm(6)! == 12

```
## Num.log

```tomo
Num.log : func(x: Num -> Num?)
```

The natural logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose logarithm is to be computed.  | -

**Return:** The natural log of `x`, or `none` if `x` is not positive.


**Example:**
```tomo
assert (1.).log()! == 0
assert (0.).log() == none

```
## Num.log10

```tomo
Num.log10 : func(x: Num -> Num?)
```

The base-10 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose logarithm is to be computed.  | -

**Return:** The base-10 log of `x`, or `none` if `x` is not positive.


**Example:**
```tomo
assert (100.).log10()! == 2

```
## Num.log2

```tomo
Num.log2 : func(x: Num -> Num?)
```

The base-2 logarithm of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose logarithm is to be computed.  | -

**Return:** The base-2 log of `x`, or `none` if `x` is not positive.


**Example:**
```tomo
assert (8.).log2()! == 3
assert (1./4.).log2()! == -2

```
## Num.max

```tomo
Num.max : func(x: Num, y: Num -> Num)
```

The larger of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -

**Return:** Whichever of `x` and `y` is larger.


**Example:**
```tomo
assert (0.3).max(0.5) == 0.5

```
## Num.min

```tomo
Num.min : func(x: Num, y: Num -> Num)
```

The smaller of two numbers.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The first number.  | -
y | `Num` | The second number.  | -

**Return:** Whichever of `x` and `y` is smaller.


**Example:**
```tomo
assert (0.3).min(0.5) == 0.3

```
## Num.mix

```tomo
Num.mix : func(amount: Num, x: Num, y: Num -> Num)
```

A point between two numbers, exactly. `amount` of zero gives `x`, one gives `y`, and a half gives the true midpoint.

Argument | Type | Description | Default
---------|------|-------------|---------
amount | `Num` | How far from `x` toward `y`.  | -
x | `Num` | The value at `amount` zero.  | -
y | `Num` | The value at `amount` one.  | -

**Return:** The interpolated value.


**Example:**
```tomo
assert (0.25).mix(10, 20) == 12.5
assert (1./3.).mix(0, 1) == 1./3.

```
## Num.parse

```tomo
Num.parse : func(text: Text -> Num?)
```

The exact number a piece of text denotes. Both decimals and fractions are accepted, and both are read exactly: "0.1" is a tenth, not the nearest float to one.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to parse.  | -

**Return:** The number, or `none` if the text isn't one.


**Example:**
```tomo
assert Num.parse("0.1")! == 0.1
assert Num.parse("22/7")! == 22./7.
assert Num.parse("nope") == none

```
## Num.percent

```tomo
Num.percent : func(n: Num, precision: Num = 1% -> Text)
```

A number as a percentage, rounded to the given precision.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to format.  | -
precision | `Num` | Round the percentage to this precision.  | `1%`

**Return:** The percentage, with a percent sign.


**Example:**
```tomo
assert (0.5).percent() == "50%"
assert (1./3.).percent() == "33%"
assert (1./3.).percent(0.01%) == "33.33%"

```
## Num.round

```tomo
Num.round : func(x: Num -> Num)
```

The nearest whole number, with halves going to the even one.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to round.  | -

**Return:** `x` rounded.


**Example:**
```tomo
assert (4.4).round() == 4
assert (4.6).round() == 5
assert (2.5).round() == 2

```
## Num.sin

```tomo
Num.sin : func(x: Num -> Num)
```

The sine of an angle in radians.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The angle in radians.  | -

**Return:** The sine of `x`.


**Example:**
```tomo
assert (0.).sin() == 0
assert Num.PI.sin() == 0

```
## Num.sinh

```tomo
Num.sinh : func(x: Num -> Num)
```

The hyperbolic sine of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose hyperbolic sine is to be computed.  | -

**Return:** The hyperbolic sine of `x`.


**Example:**
```tomo
assert (0.).sinh() == 0

```
## Num.sqrt

```tomo
Num.sqrt : func(x: Num -> Num?)
```

The square root of a number, exactly. The result of a non-square stays symbolic, so identities like `sqrt(2) * sqrt(2) == 2` hold.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose square root is to be computed.  | -

**Return:** The square root of `x`, or `none` if `x` is negative.


**Example:**
```tomo
assert (16.).sqrt()! == 4
assert (2.).sqrt()! * (2.).sqrt()! == 2
assert (-1.).sqrt() == none

```
## Num.symbolic

```tomo
Num.symbolic : func(n: Num -> Text)
```

The exact value written out: a whole number, a fraction, or a symbolic expression for an irrational.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to write out.  | -

**Return:** The exact form.


**Example:**
```tomo
assert (1./3.).symbolic() == "1/3"
assert (2.).sqrt()!.symbolic() == "sqrt(2)"
assert Num.PI.symbolic() == "pi"

```
## Num.tan

```tomo
Num.tan : func(x: Num -> Num?)
```

The tangent of an angle in radians.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The angle in radians.  | -

**Return:** The tangent of `x`, or `none` at a pole.


**Example:**
```tomo
assert (0.).tan()! == 0

```
## Num.tanh

```tomo
Num.tanh : func(x: Num -> Num)
```

The hyperbolic tangent of a number.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number whose hyperbolic tangent is to be computed.  | -

**Return:** The hyperbolic tangent of `x`.


**Example:**
```tomo
assert (0.).tanh() == 0

```
## Num.tex

```tomo
Num.tex : func(n: Num -> Text)
```

The exact value as TeX math-mode source.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Num` | The number to render.  | -

**Return:** The TeX source.


**Example:**
```tomo
assert (1./3.).tex() == "\\frac{1}{3}"
assert (2.).sqrt()!.tex() == "\\sqrt{2}"

```
## Num.trunc

```tomo
Num.trunc : func(x: Num -> Num)
```

The whole part of a number, discarding the fraction.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Num` | The number to truncate.  | -

**Return:** `x` with its fractional part removed.


**Example:**
```tomo
assert (4.8).trunc() == 4
assert (-4.8).trunc() == -4

```
