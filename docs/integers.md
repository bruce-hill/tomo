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

## Integer Functions

Each integer type has its own version of the following functions. Functions
can be called either on the type itself: `Int.sqrt(x)` or as a method call:
`x.sqrt()`. Method call syntax is preferred.

- [`func abs(x: Int -> Int)`](#abs)
- [`func choose(n: Int, k: Int -> Int)`](#choose)
- [`func clamped(x, low, high: Int -> Int)`](#clamped)
- [`func factorial(n: Int -> Text)`](#factorial)
- [`func format(i: Int, digits: Int = 0 -> Text)`](#format)
- [`func hex(i: Int, digits: Int = 0, uppercase: Bool = yes, prefix: Bool = yes -> Text)`](#hex)
- [`func is_between(x: Int, low: Int, high: Int -> Bool)`](#is_between)
- [`func is_prime(x: Int, reps: Int = 50 -> Bool)`](#is_prime)
- [`func next_prime(x: Int -> Int)`](#next_prime)
- [`func octal(i: Int, digits: Int = 0, prefix: Bool = yes -> Text)`](#octal)
- [`func onward(first: Int, step: Int = 1 -> Text)`](#onward)
- [`func parse(text: Text -> Int?)`](#parse)
- [`func prev_prime(x: Int -> Int?)`](#prev_prime)
- [`func sqrt(x: Int -> Int)`](#sqrt)
- [`func to(first: Int, last: Int, step : Int? = none -> func(->Int?))`](#to)

### `abs`
Calculates the absolute value of an integer.

```tomo
func abs(x: Int -> Int)
```

- `x`: The integer whose absolute value is to be calculated.

**Returns:**  
The absolute value of `x`.

**Example:**  
```tomo
>> (-10).abs()
= 10
```

---

### `choose`
Computes the binomial coefficient of the given numbers (the equivalent of `n`
choose `k` in combinatorics). This is equal to `n.factorial()/(k.factorial() *
(n-k).factorial())`.

```tomo
func choose(n: Int, k: Int -> Int)
```

- `n`: The number of things to choose from.
- `k`: The number of things to be chosen.

**Returns:**  
The binomial coefficient, equivalent to the number of ways to uniquely choose
`k` objects from among `n` objects, ignoring order.

**Example:**  
```tomo
>> (4).choose(2)
= 6
```

---

### `clamped`
Returns the given number clamped between two values so that it is within
that range.

```tomo
func clamped(x, low, high: Int -> Int)
```

- `x`: The integer to clamp.
- `low`: The lowest value the result can take.
- `high`: The highest value the result can take.

**Returns:**  
The first argument clamped between the other two arguments.

**Example:**  
```tomo
>> (2).clamped(5, 10)
= 5
```

---

### `factorial`
Computes the factorial of an integer.

```tomo
func factorial(n: Int -> Text)
```

- `n`: The integer to compute the factorial of.

**Returns:**  
The factorial of the given integer.

**Example:**  
```tomo
>> (10).factorial()
= 3628800
```

---

### `format`
Formats an integer as a string with a specified number of digits.

```tomo
func format(i: Int, digits: Int = 0 -> Text)
```

- `i`: The integer to be formatted.
- `digits`: The minimum number of digits to which the integer should be padded. Default is `0`.

**Returns:**  
A string representation of the integer, padded to the specified number of digits.

**Example:**  
```tomo
>> (42).format(digits=5)
= "00042"
```

---

### `hex`
Converts an integer to its hexadecimal representation.

```tomo
func hex(i: Int, digits: Int = 0, uppercase: Bool = yes, prefix: Bool = yes -> Text)
```

- `i`: The integer to be converted.
- `digits`: The minimum number of digits in the output string. Default is `0`.
- `uppercase`: Whether to use uppercase letters for hexadecimal digits. Default is `yes`.
- `prefix`: Whether to include a "0x" prefix. Default is `yes`.

**Returns:**  
The hexadecimal string representation of the integer.

**Example:**  
```tomo
>> (255).hex(digits=4, uppercase=yes, prefix=yes)
= "0x00FF"
```

---

### `is_between`
Determines if an integer is between two numbers (inclusive).

```tomo
func is_between(x: Int, low: Int, high: Int -> Bool)
```

- `x`: The integer to be checked.
- `low`: The lower bound to check (inclusive).
- `high`: The upper bound to check (inclusive).

**Returns:**  
`yes` if `low <= x and x <= high`, otherwise `no`

**Example:**  
```tomo
>> (7).is_between(1, 10)
= yes
>> (7).is_between(100, 200)
= no
>> (7).is_between(1, 7)
= yes
```

---

### `is_prime`
Determines if an integer is a prime number.

**Note:**
This function is _probabilistic_. With the default arguments, the chances of
getting an incorrect answer are astronomically small (on the order of 10^(-30)).
See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp)
for more details.

```tomo
func is_prime(x: Int, reps: Int = 50 -> Bool)
```

- `x`: The integer to be checked.
- `reps`: The number of repetitions for primality tests. Default is `50`.

**Returns:**  
`yes` if `x` is a prime number, `no` otherwise.

**Example:**  
```tomo
>> (7).is_prime()
= yes
>> (6).is_prime()
= no
```

---

### `next_prime`
Finds the next prime number greater than the given integer.

**Note:**
This function is _probabilistic_, but the chances of getting an incorrect
answer are astronomically small (on the order of 10^(-30)).
See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp)
for more details.

```tomo
func next_prime(x: Int -> Int)
```

- `x`: The integer after which to find the next prime.

**Returns:**  
The next prime number greater than `x`.

**Example:**  
```tomo
>> (11).next_prime()
= 13
```

---

### `octal`
Converts an integer to its octal representation.

```tomo
func octal(i: Int, digits: Int = 0, prefix: Bool = yes -> Text)
```

- `i`: The integer to be converted.
- `digits`: The minimum number of digits in the output string. Default is `0`.
- `prefix`: Whether to include a "0o" prefix. Default is `yes`.

**Returns:**  
The octal string representation of the integer.

**Example:**  
```tomo
>> (64).octal(digits=4, prefix=yes)
= "0o0100"
```

---

### `onward`
Return an iterator that counts infinitely from the starting integer (with an
optional step size).

```tomo
func onward(first: Int, step: Int = 1 -> Text)
```

- `first`: The starting integer.
- `step`: The increment step size (default: 1).

**Returns:**  
An iterator function that counts onward from the starting integer.

**Example:**  
```tomo
nums : &[Int] = &[]
for i in (5).onward()
    nums.insert(i)
    stop if i == 10
>> nums[]
= [5, 6, 7, 8, 9, 10]
```

---

### `parse`
Converts a text representation of an integer into an integer.

```tomo
func parse(text: Text -> Int?)
```

- `text`: The text containing the integer.

**Returns:**  
The integer represented by the text. If the given text contains a value outside
of the representable range or if the entire text can't be parsed as an integer,
`none` will be returned.

**Example:**  
```tomo
>> Int.parse("123")
= 123 : Int?
>> Int.parse("0xFF")
= 255 : Int?

# Can't parse:
>> Int.parse("asdf")
= none : Int?

# Outside valid range:
>> Int8.parse("9999999")
= none : Int8?
```

---

### `prev_prime`
Finds the previous prime number less than the given integer.
If there is no previous prime number (i.e. if a number less than `2` is
provided), then the function will create a runtime error.

**Note:**
This function is _probabilistic_, but the chances of getting an incorrect
answer are astronomically small (on the order of 10^(-30)).
See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp)
for more details.

```tomo
func prev_prime(x: Int -> Int?)
```

- `x`: The integer before which to find the previous prime.

**Returns:**  
The previous prime number less than `x`, or `none` if `x` is less than 2.

**Example:**  
```tomo
>> (11).prev_prime()
= 7
```

---

### `sqrt`
Calculates the square root of an integer.

```tomo
func sqrt(x: Int -> Int)
```

- `x`: The integer whose square root is to be calculated.

**Returns:**  
The integer part of the square root of `x`.

**Example:**  
```tomo
>> (16).sqrt()
= 4
>> (17).sqrt()
= 4
```

---

### `to`
Returns an iterator function that iterates over the range of numbers specified.
Iteration is assumed to be nonempty and

```tomo
func to(first: Int, last: Int, step : Int? = none -> func(->Int?))
```

- `first`: The starting value of the range.
- `last`: The ending value of the range.
- `step`: An optional step size to use. If unspecified or `none`, the step will be inferred to be `+1` if `last >= first`, otherwise `-1`.

**Returns:**  
An iterator function that returns each integer in the given range (inclusive).

**Example:**  
```tomo
>> (2).to(5)
= func(->Int?)
>> [x for x in (2).to(5)]
= [2, 3, 4, 5]
>> [x for x in (5).to(2)]
= [5, 4, 3, 2]

>> [x for x in (2).to(5, step=2)]
= [2, 4]
```
