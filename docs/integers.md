# Integers

Tomo has five types of integers:

- `Int`: the default integer type, which uses an efficient tagged 29-bit
  integer value for small numbers, and falls back to a bigint implementation
  when values are too large to fit in 29-bits. The bigint implementation uses
  the GNU MP library. These integers are fast for small numbers and guaranteed
  to always be correct and never overflow.
- `Int8`/`Int16`/`Int32`/`Int64`: Fixed-size integers that take up `N` bits.
  These integers must be manually specified with their bits in square brackets
  (e.g. `5[64]`) and are subject to overflowing. If an overflow occurs, a
  runtime error will be raised.

Conversion between integer types can be done by calling the target type as a
function: `Int32(x)`. For fixed-width types, the conversion function also
accepts a second parameter, `truncate`. If `truncate` is `no` (the default),
conversion will create a runtime error if the value is too large to fit in the
target type. If `truncate` is `yes`, then the resulting value will be a
truncated form of the input value.

Integers support the standard math operations (`x+y`, `x-y`, `x*y`, `x/y`) as
well as powers/exponentiation (`x^y`), modulus (`x mod y` and `x mod1 y`), and
bitwise operations: `x and y`, `x or y`, `x xor y`, `x << y`, and `x >> y`. The
operators `and`, `or`, and `xor` are _bitwise_, not logical operators.

# Integer Functions

Each integer type has its own version of the following functions. Functions
can be called either on the type itself: `Int.sqrt(x)` or as a method call:
`x:sqrt()`. Method call syntax is preferred.

## `format`

**Description:**  
Formats an integer as a string with a specified number of digits.

**Usage:**  
```tomo
format(i: Int, digits: Int = 0) -> Text
```

**Parameters:**

- `i`: The integer to be formatted.
- `digits`: The minimum number of digits to which the integer should be padded. Default is `0`.

**Returns:**  
A string representation of the integer, padded to the specified number of digits.

**Example:**  
```tomo
>> 42:format(digits=5)
= "00042"
```

---

## `hex`

**Description:**  
Converts an integer to its hexadecimal representation.

**Usage:**  
```tomo
hex(i: Int, digits: Int = 0, uppercase: Bool = yes, prefix: Bool = yes) -> Text
```

**Parameters:**

- `i`: The integer to be converted.
- `digits`: The minimum number of digits in the output string. Default is `0`.
- `uppercase`: Whether to use uppercase letters for hexadecimal digits. Default is `yes`.
- `prefix`: Whether to include a "0x" prefix. Default is `yes`.

**Returns:**  
The hexadecimal string representation of the integer.

**Example:**  
```tomo
>> 255:hex(digits=4, uppercase=yes, prefix=yes)
= "0x00FF"
```

---

## `octal`

**Description:**  
Converts an integer to its octal representation.

**Usage:**  
```tomo
octal(i: Int, digits: Int = 0, prefix: Bool = yes) -> Text
```

**Parameters:**

- `i`: The integer to be converted.
- `digits`: The minimum number of digits in the output string. Default is `0`.
- `prefix`: Whether to include a "0o" prefix. Default is `yes`.

**Returns:**  
The octal string representation of the integer.

**Example:**  
```tomo
>> 64:octal(digits=4, prefix=yes)
= "0o0100"
```

---

## `random`

**Description:**  
Generates a random integer between the specified minimum and maximum values.

**Usage:**  
```tomo
random(min: Int, max: Int) -> Int
```

**Parameters:**

- `min`: The minimum value of the range.
- `max`: The maximum value of the range.

**Returns:**  
A random integer between `min` and `max` (inclusive).

**Example:**  
```tomo
>> Int.random(1, 100)
= 47
```

---

## `from_text`

**Description:**  
Converts a text representation of an integer into an integer.

**Usage:**  
```tomo
from_text(text: Text, success: Bool = !&Bool?) -> Int
```

**Parameters:**

- `text`: The text containing the integer.
- `success`: If non-null, this pointer will be set to `yes` if the whole text
  is a valid integer that fits within the representable range of the integer
  type, otherwise `no`.

**Returns:**  
The integer represented by the text. If the given text contains a value outside
of the representable range, the number will be truncated to the minimum or
maximum representable value. Other failures to parse the number will return
zero.

**Example:**  
```tomo
>> Int.from_text("123")
= 123
>> Int.from_text("0xFF")
= 255

success := no
>> Int.from_text("asdf", &success)
= 0
>> success
= no

>> Int8.from_text("9999999", &success)
= 127
>> success
= no
```

---

## `to`

**Description:**  
Creates an inclusive range of integers between the specified start and end values.

**Usage:**  
```tomo
to(from: Int, to: Int) -> Range
```

**Parameters:**

- `from`: The starting value of the range.
- `to`: The ending value of the range.

**Returns:**  
A range object representing all integers from `from` to `to` (inclusive).

**Example:**  
```tomo
>> 1:to(5)
= Range(first=1, last=5, step=1)
```

---

## `abs`

**Description:**  
Calculates the absolute value of an integer.

**Usage:**  
```tomo
abs(x: Int) -> Int
```

**Parameters:**

- `x`: The integer whose absolute value is to be calculated.

**Returns:**  
The absolute value of `x`.

**Example:**  
```tomo
>> -10:abs()
= 10
```

---

## `sqrt`

**Description:**  
Calculates the square root of an integer. 

**Usage:**  
```tomo
sqrt(x: Int) -> Int
```

**Parameters:**

- `x`: The integer whose square root is to be calculated.

**Returns:**  
The integer part of the square root of `x`.

**Example:**  
```tomo
>> 16:sqrt()
= 4
>> 17:sqrt()
= 4
```

---

## `is_prime`

**Description:**  
Determines if an integer is a prime number.

**Note:**
This function is _probabilistic_. With the default arguments, the chances of
getting an incorrect answer are astronomically small (on the order of 10^(-30)).
See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp)
for more details.

**Usage:**  
```tomo
is_prime(x: Int, reps: Int = 50) -> Bool
```

**Parameters:**

- `x`: The integer to be checked.
- `reps`: The number of repetitions for primality tests. Default is `50`.

**Returns:**  
`yes` if `x` is a prime number, `no` otherwise.

**Example:**  
```tomo
>> 7:is_prime()
= yes
>> 6:is_prime()
= no
```

---

## `next_prime`

**Description:**  
Finds the next prime number greater than the given integer.

**Note:**
This function is _probabilistic_, but the chances of getting an incorrect
answer are astronomically small (on the order of 10^(-30)).
See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp)
for more details.

**Usage:**  
```tomo
next_prime(x: Int) -> Int
```

**Parameters:**

- `x`: The integer after which to find the next prime.

**Returns:**  
The next prime number greater than `x`.

**Example:**  
```tomo
>> 11:next_prime()
= 13
```

---

## `prev_prime`

**Description:**  
Finds the previous prime number less than the given integer.
If there is no previous prime number (i.e. if a number less than `2` is
provided), then the function will create a runtime error.

**Note:**
This function is _probabilistic_, but the chances of getting an incorrect
answer are astronomically small (on the order of 10^(-30)).
See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp)
for more details.

**Usage:**  
```tomo
prev_prime(x: Int) -> Int
```

**Parameters:**

- `x`: The integer before which to find the previous prime.

**Returns:**  
The previous prime number less than `x`.

**Example:**  
```tomo
>> 11:prev_prime()
= 7
```

---

## `clamped`

**Description:**  
Returns the given number clamped between two values so that it is within
that range.

**Usage:**  
```tomo
clamped(x, low, high: Int) -> Int
```

**Parameters:**

- `x`: The integer to clamp.
- `low`: The lowest value the result can take.
- `high`: The highest value the result can take.

**Returns:**  
The first argument clamped between the other two arguments.

**Example:**  
```tomo
>> 2:clamped(5, 10)
= 5
```
