# Nums

Tomo has two floating point number types: `Num` (64-bit, AKA `double`) and
`Num32` (32-bit, AKA `float`). Num literals can have a decimal point (e.g.
`5.`), a scientific notation suffix (e.g. `1e8`) or a percent sign. Numbers
that end in a percent sign are divided by 100 at compile time (i.e. `5% ==
0.05`).

Nums support the standard math operations (`x+y`, `x-y`, `x*y`, `x/y`) as well as
powers/exponentiation (`x^y`) and modulus (`x mod y` and `x mod1 y`).

# Num Functions

Each Num type has its own version of the following functions. Functions can be
called either on the type itself: `Num.sqrt(x)` or as a method call:
`x:sqrt()`. Method call syntax is preferred.

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

### `abs`

**Description:**
Calculates the absolute value of a number.

**Usage:**
```tomo
abs(n: Num) -> Num
```

**Parameters:**

- `n`: The number whose absolute value is to be computed.

**Returns:**
The absolute value of `n`.

**Example:**
```tomo
>> -3.5:abs()
= 3.5
```

---

### `acos`

**Description:**
Computes the arc cosine of a number.

**Usage:**
```tomo
acos(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the arc cosine is to be calculated.

**Returns:**
The arc cosine of `x` in radians.

**Example:**
```tomo
>> 0.0:acos() // -> (π/2)
= 1.5708
```

---

### `acosh`

**Description:**
Computes the inverse hyperbolic cosine of a number.

**Usage:**
```tomo
acosh(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the inverse hyperbolic cosine is to be calculated.

**Returns:**
The inverse hyperbolic cosine of `x`.

**Example:**
```tomo
>> 1.0:acosh()
= 0
```

---

### `asin`

**Description:**
Computes the arc sine of a number.

**Usage:**
```tomo
asin(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the arc sine is to be calculated.

**Returns:**
The arc sine of `x` in radians.

**Example:**
```tomo
>> 0.5:asin()  // -> (π/6)
= 0.5236
```

---

### `asinh`

**Description:**
Computes the inverse hyperbolic sine of a number.

**Usage:**
```tomo
asinh(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the inverse hyperbolic sine is to be calculated.

**Returns:**
The inverse hyperbolic sine of `x`.

**Example:**
```tomo
>> 0.0:asinh()
= 0
```

---

### `atan2`

**Description:**
Computes the arc tangent of the quotient of two numbers.

**Usage:**
```tomo
atan2(x: Num, y: Num) -> Num
```

**Parameters:**

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

### `atan`

**Description:**
Computes the arc tangent of a number.

**Usage:**
```tomo
atan(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the arc tangent is to be calculated.

**Returns:**
The arc tangent of `x` in radians.

**Example:**
```tomo
>> 1.0:atan() // -> (π/4)
= 0.7854
```

---

### `atanh`

**Description:**
Computes the inverse hyperbolic tangent of a number.

**Usage:**
```tomo
atanh(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the inverse hyperbolic tangent is to be calculated.

**Returns:**
The inverse hyperbolic tangent of `x`.

**Example:**
```tomo
>> 0.5:atanh()
= 0.5493
```

---

### `cbrt`

**Description:**
Computes the cube root of a number.

**Usage:**
```tomo
cbrt(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the cube root is to be calculated.

**Returns:**
The cube root of `x`.

**Example:**
```tomo
>> 27.0:cbrt()
= 3
```

---

### `ceil`

**Description:**
Rounds a number up to the nearest integer.

**Usage:**
```tomo
ceil(x: Num) -> Num
```

**Parameters:**

- `x`: The number to be rounded up.

**Returns:**
The smallest integer greater than or equal to `x`.

**Example:**
```tomo
>> 3.2:ceil()
= 4
```

---

### `copysign`

**Description:**
Copies the sign of one number to another.

**Usage:**
```tomo
copysign(x: Num, y: Num) -> Num
```

**Parameters:**

- `x`: The number whose magnitude will be copied.
- `y`: The number whose sign will be copied.

**Returns:**
A number with the magnitude of `x` and the sign of `y`.

**Example:**
```tomo
>> 3.0:copysign(-1)
= -3
```

---

### `cos`

**Description:**
Computes the cosine of a number (angle in radians).

**Usage:**
```tomo
cos(x: Num) -> Num
```

**Parameters:**

- `x`: The angle in radians.

**Returns:**
The cosine of `x`.

**Example:**
```tomo
>> 0.0:cos()
= 1
```

---

### `cosh`

**Description:**
Computes the hyperbolic cosine of a number.

**Usage:**
```tomo
cosh(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the hyperbolic cosine is to be calculated.

**Returns:**
The hyperbolic cosine of `x`.

**Example:**
```tomo
>> 0.0:cosh()
= 1
```

---

### `erf`

**Description:**
Computes the error function of a number.

**Usage:**
```tomo
erf(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the error function is to be calculated.

**Returns:**
The error function of `x`.

**Example:**
```tomo
>> 0.0:erf()
= 0
```

---

### `erfc`

**Description:**
Computes the complementary error function of a number.

**Usage:**
```tomo
erfc(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the complementary error function is to be calculated.

**Returns:**
The complementary error function of `x`.

**Example:**
```tomo
>> 0.0:erfc()
= 1
```

---

### `exp2`

**Description:**
Computes \( 2^x \) for a number.

**Usage:**
```tomo
exp2(x: Num) -> Num
```

**Parameters:**

- `x`: The exponent.

**Returns:**
The value of \( 2^x \).

**Example:**
```tomo
>> 3.0:exp2()
= 8
```

---

### `exp`

**Description:**
Computes the exponential function \( e^x \) for a number.

**Usage:**
```tomo
exp(x: Num) -> Num
```

**Parameters:**

- `x`: The exponent.

**Returns:**
The value of \( e^x \).

**Example:**
```tomo
>> 1.0:exp()
= 2.7183
```

---

### `expm1`

**Description:**
Computes \( e^x - 1 \) for a number.

**Usage:**
```tomo
expm1(x: Num) -> Num
```

**Parameters:**

- `x`: The exponent.

**Returns:**
The value of \( e^x - 1 \).

**Example:**
```tomo
>> 1.0:expm1()
= 1.7183
```

---

### `fdim`

**Description:**
Computes the positive difference between two numbers.

**Usage:**
```tomo
fdim(x: Num, y: Num) -> Num
```

**Parameters:**

- `x`: The first number.
- `y`: The second number.

**Returns:**
The positive difference \( \max(0, x - y) \).

**Example:**
```tomo
fd

>> 5.0:fdim(3)
= 2
```

---

### `floor`

**Description:**
Rounds a number down to the nearest integer.

**Usage:**
```tomo
floor(x: Num) -> Num
```

**Parameters:**

- `x`: The number to be rounded down.

**Returns:**
The largest integer less than or equal to `x`.

**Example:**
```tomo
>> 3.7:floor()
= 3
```

---

### `format`

**Description:**
Formats a number as a string with a specified precision.

**Usage:**
```tomo
format(n: Num, precision: Int = 0) -> Text
```

**Parameters:**

- `n`: The number to be formatted.
- `precision`: The number of decimal places. Default is `0`.

**Returns:**
A string representation of the number with the specified precision.

**Example:**
```tomo
>> 3.14159:format(precision=2)
= "3.14"
```

---

### `from_text`

**Description:**
Converts a string representation of a number into a floating-point number.

**Usage:**
```tomo
from_text(text: Text, the_rest: Text = "!&Text") -> Num
```

**Parameters:**

- `text`: The string containing the number.
- `the_rest`: A string indicating what to return if the conversion fails. Default is `"!&Text"`.

**Returns:**
The number represented by the string.

**Example:**
```tomo
>> Num.from_text("3.14")
= 3.14
>> Num.from_text("1e3")
= 1000
```

---

### `hypot`

**Description:**
Computes the Euclidean norm, \( \sqrt{x^2 + y^2} \), of two numbers.

**Usage:**
```tomo
hypot(x: Num, y: Num) -> Num
```

**Parameters:**

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

**Description:**
Checks if a number is finite.

**Usage:**
```tomo
isfinite(n: Num) -> Bool
```

**Parameters:**

- `n`: The number to be checked.

**Returns:**
`yes` if `n` is finite, `no` otherwise.

**Example:**
```tomo
>> 1.0:isfinite()
= yes
>> Num.INF:isfinite()
= no
```

---

### `isinf`

**Description:**
Checks if a number is infinite.

**Usage:**
```tomo
isinf(n: Num) -> Bool
```

**Parameters:**

- `n`: The number to be checked.

**Returns:**
`yes` if `n` is infinite, `no` otherwise.

**Example:**
```tomo
>> Num.INF:isinf()
= yes
>> 1.0:isinf()
= no
```

---

### `isnan`

**Description:**
Checks if a number is NaN (Not a Number).

**Usage:**
```tomo
isnan(n: Num) -> Bool
```

**Parameters:**

- `n`: The number to be checked.

**Returns:**
`yes` if `n` is NaN, `no` otherwise.

**Example:**
```tomo
>> Num.nan():isnan()
= yes
>> 1.0:isnan()
= no
```

---

### `j0`

**Description:**
Computes the Bessel function of the first kind of order 0.

**Usage:**
```tomo
j0(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the first kind of order 0 of `x`.

**Example:**
```tomo
>> 0.0:j0()
= 1
```

---

### `j1`

**Description:**
Computes the Bessel function of the first kind of order 1.

**Usage:**
```tomo
j1(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the first kind of order 1 of `x`.

**Example:**
```tomo
>> 0.0:j1()
= 0
```

---

### `log10`

**Description:**
Computes the base-10 logarithm of a number.

**Usage:**
```tomo
log10(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the base-10 logarithm is to be calculated.

**Returns:**
The base-10 logarithm of `x`.

**Example:**
```tomo
>> 100.0:log10()
= 2
```

---

### `log1p`

**Description:**
Computes \( \log(1 + x) \) for a number.

**Usage:**
```tomo
log1p(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which \( \log(1 + x) \) is to be calculated.

**Returns:**
The value of \( \log(1 + x) \).

**Example:**
```tomo
>> 1.0:log1p()
= 0.6931
```

---

### `log2`

**Description:**
Computes the base-2 logarithm of a number.

**Usage:**
```tomo
log2(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the base-2 logarithm is to be calculated.

**Returns:**
The base-2 logarithm of `x`.

**Example:**
```tomo
>> 8.0:log2()
= 3
```

---

### `log`

**Description:**
Computes the natural logarithm (base \( e \)) of a number.

**Usage:**
```tomo
log(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the natural logarithm is to be calculated.

**Returns:**
The natural logarithm of `x`.

**Example:**
```tomo
>> Num.E:log()
= 1
```

---

### `logb`

**Description:**
Computes the binary exponent (base-2 logarithm) of a number.

**Usage:**
```tomo
logb(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the binary exponent is to be calculated.

**Returns:**
The binary exponent of `x`.

**Example:**
```tomo
>> 8.0:logb()
= 3
```

---

### `mix`

**Description:**
Interpolates between two numbers based on a given amount.

**Usage:**
```tomo
mix(amount: Num, x: Num, y: Num) -> Num
```

**Parameters:**

- `amount`: The interpolation factor (between `0` and `1`).
- `x`: The starting number.
- `y`: The ending number.

**Returns:**
The interpolated number between `x` and `y` based on `amount`.

**Example:**
```tomo
>> 0.5:mix(10, 20)
= 15
>> 0.25:mix(10, 20)
= 12.5
```

---

### `nan`

**Description:**
Generates a NaN (Not a Number) value.

**Usage:**
```tomo
nan(tag: Text = "") -> Num
```

**Parameters:**

- `tag`: An optional tag to describe the NaN. Default is an empty string.

**Returns:**
A NaN value.

**Example:**
```tomo
>> Num.nan()
= NaN
```

---

### `near`

**Description:**
Checks if two numbers are approximately equal within specified tolerances. If
two numbers are within an absolute difference or the ratio between the two is
small enough, they are considered near each other.

**Usage:**
```tomo
near(x: Num, y: Num, ratio: Num = 1e-9, min_epsilon: Num = 1e-9) -> Bool
```

**Parameters:**

- `x`: The first number.
- `y`: The second number.
- `ratio`: The relative tolerance. Default is `1e-9`.
- `min_epsilon`: The absolute tolerance. Default is `1e-9`.

**Returns:**
`yes` if `x` and `y` are approximately equal within the specified tolerances, `no` otherwise.

**Example:**
```tomo
>> 1.0:near(1.000000001)
= yes

>> 100.0:near(110, ratio=0.1)
= yes

>> 5.0:near(5.1, min_epsilon=0.1)
= yes
```

---

### `nextafter`

**Description:**
Computes the next representable value after a given number towards a specified direction.

**Usage:**
```tomo
nextafter(x: Num, y: Num) -> Num
```

**Parameters:**

- `x`: The starting number.
- `y`: The direction towards which to find the next representable value.

**Returns:**
The next representable value after `x` in the direction of `y`.

**Example:**
```tomo
>> 1.0:nextafter(1.1)
= 1.0000000000000002
```

---

### `random`

**Description:**
Generates a random floating-point number.

**Usage:**
```tomo
random() -> Num
```

**Parameters:**
None

**Returns:**
A random floating-point number between 0 and 1.

**Example:**
```tomo
>> Num.random()
= 0.4521
```

---

### `rint`

**Description:**
Rounds a number to the nearest integer, with ties rounded to the nearest even integer.

**Usage:**
```tomo
rint(x: Num) -> Num
```

**Parameters:**

- `x`: The number to be rounded.

**Returns:**
The nearest integer value of `x`.

**Example:**
```tomo
>> 3.5:rint()
= 4
>> 2.5:rint()
= 2
```

---

### `round`

**Description:**
Rounds a number to the nearest whole number integer.

**Usage:**
```tomo
round(x: Num) -> Num
```

**Parameters:**

- `x`: The number to be rounded.

**Returns:**
The nearest integer value of `x`.

**Example:**
```tomo
>> 2.3:round()
= 2
>> 2.7:round()
= 3
```

---

### `scientific`

**Description:**
Formats a number in scientific notation with a specified precision.

**Usage:**
```tomo
scientific(n: Num, precision: Int = 0) -> Text
```

**Parameters:**

- `n`: The number to be formatted.
- `precision`: The number of decimal places. Default is `0`.

**Returns:**
A string representation of the number in scientific notation with the specified precision.

**Example:**
```tomo
>> 12345.6789:scientific(precision=2)
= "1.23e+04"
```

---

### `significand`

**Description:**
Extracts the significand (or mantissa) of a number.

**Usage:**
```tomo
significand(x: Num) -> Num
```

**Parameters:**

- `x`: The number from which to extract the significand.

**Returns:**
The significand of `x`.

**Example:**
```tomo
>> 1234.567:significand()
= 0.1234567
```

---

### `sin`

**Description:**
Computes the sine of a number (angle in radians).

**Usage:**
```tomo
sin(x: Num) -> Num
```

**Parameters:**

- `x`: The angle in radians.

**Returns:**
The sine of `x`.

**Example:**
```tomo
>> 0.0:sin()
= 0
```

---

### `sinh`

**Description:**
Computes the hyperbolic sine of a number.

**Usage:**
```tomo
sinh(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the hyperbolic sine is to be calculated.

**Returns:**
The hyperbolic sine of `x`.

**Example:**
```tomo
>> 0.0:sinh()
= 0
```

---

### `sqrt`

**Description:**
Computes the square root of a number.

**Usage:**
```tomo
sqrt(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the square root is to be calculated.

**Returns:**
The square root of `x`.

**Example:**
```tomo
>> 16.0:sqrt()
= 4
```

---

### `tan`

**Description:**
Computes the tangent of a number (angle in radians).

**Usage:**
```tomo
tan(x: Num) -> Num
```

**Parameters:**

- `x`: The angle in radians.

**Returns:**
The tangent of `x`.

**Example:**
```tomo
>> 0.0:tan()
= 0
```

---

### `tanh`

**Description:**
Computes the hyperbolic tangent of a number.

**Usage:**
```tomo
tanh(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the hyperbolic tangent is to be calculated.

**Returns:**
The hyperbolic tangent of `x`.

**Example:**
```tomo
>> 0.0:tanh()
= 0
```

---

### `tgamma`

**Description:**
Computes the gamma function of a number.

**Usage:**
```tomo
tgamma(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the gamma function is to be calculated.

**Returns:**
The gamma function of `x`.

**Example:**
```tomo
>> 1.0:tgamma()
= 1
```

---

### `trunc`

**Description:**
Truncates a number to the nearest integer towards zero.

**Usage:**
```tomo
trunc(x: Num) -> Num
```

**Parameters:**

- `x`: The number to be truncated.

**Returns:**
The integer part of `x` towards zero.

**Example:**
```tomo
>> 3.7:trunc()
= 3
>> (-3.7):trunc()
= -3
```

---

### `y0`

**Description:**
Computes the Bessel function of the second kind of order 0.

**Usage:**
```tomo
y0(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the second kind of order 0 of `x`.

**Example:**
```tomo
>> 1.0:y0()
= -0.7652
```

---

### `y1`

**Description:**
Computes the Bessel function of the second kind of order 1.

**Usage:**
```tomo
y1(x: Num) -> Num
```

**Parameters:**

- `x`: The number for which the Bessel function is to be calculated.

**Returns:**
The Bessel function of the second kind of order 1 of `x`.

**Example:**
```tomo
>> 1.0:y1()
= 0.4401
```

---

## `clamped`

**Description:**  
Returns the given number clamped between two values so that it is within
that range.

**Usage:**  
```tomo
clamped(x, low, high: Num) -> Num
```

**Parameters:**

- `x`: The number to clamp.
- `low`: The lowest value the result can take.
- `high`: The highest value the result can take.

**Returns:**  
The first argument clamped between the other two arguments.

**Example:**  
```tomo
>> 2.5:clamped(5.5, 10.5)
= 5.5
```
