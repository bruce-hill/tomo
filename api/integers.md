% API

# Builtins

# Int
## Int.abs

```tomo
Int.abs : func(x: Int -> Int)
```

Calculates the absolute value of an integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer whose absolute value is to be calculated.  | -

**Return:** The absolute value of `x`.


**Example:**
```tomo
>> (-10).abs()
= 10

```
## Int.choose

```tomo
Int.choose : func(n: Int, k: Int -> Int)
```

Computes the binomial coefficient of the given numbers (the equivalent of `n` choose `k` in combinatorics). This is equal to `n.factorial()/(k.factorial() * (n-k).factorial())`.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Int` | The number of things to choose from.  | -
k | `Int` | The number of things to be chosen.  | -

**Return:** The binomial coefficient, equivalent to the number of ways to uniquely choose `k` objects from among `n` objects, ignoring order.


**Example:**
```tomo
>> (4).choose(2)
= 6

```
## Int.clamped

```tomo
Int.clamped : func(x: Int, low: Int, high: Int -> Int)
```

Returns the given number clamped between two values so that it is within that range.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer to clamp.  | -
low | `Int` | The lowest value the result can take.  | -
high | `Int` | The highest value the result can take.  | -

**Return:** The first argument clamped between the other two arguments.


**Example:**
```tomo
>> (2).clamped(5, 10)
= 5

```
## Int.factorial

```tomo
Int.factorial : func(n: Int -> Text)
```

Computes the factorial of an integer.

Argument | Type | Description | Default
---------|------|-------------|---------
n | `Int` | The integer to compute the factorial of.  | -

**Return:** The factorial of the given integer.


**Example:**
```tomo
>> (10).factorial()
= 3628800

```
## Int.get_bit

```tomo
Int.get_bit : func(i: Int, bit_index: Int -> Bool)
```

In the binary representation of an integer, check whether a given bit index is set to 1 or not.

For fixed-size integers, the bit index must be between 1 and the number of bits in that integer (i.e. 1-64 for `Int64`). For `Int`, the bit index must be between 1 and `Int64.max`. Values outside this range will produce a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
i | `Int` | The integer whose bits are being inspected.  | -
bit_index | `Int` | The index of the bit to check (1-indexed).  | -

**Return:** Whether or not the given bit index is set to 1 in the binary representation of the integer.


**Example:**
```tomo
>> (6).get_bit(1)
= no
>> (6).get_bit(2)
= yes
>> (6).get_bit(3)
= yes
>> (6).get_bit(4)
= no

```
## Int.hex

```tomo
Int.hex : func(i: Int, digits: Int = 0, uppercase: Bool = yes, prefix: Bool = yes -> Text)
```

Converts an integer to its hexadecimal representation.

Argument | Type | Description | Default
---------|------|-------------|---------
i | `Int` | The integer to be converted.  | -
digits | `Int` | The minimum number of digits in the output string.  | `0`
uppercase | `Bool` | Whether to use uppercase letters for hexadecimal digits.  | `yes`
prefix | `Bool` | Whether to include a "0x" prefix.  | `yes`

**Return:** The hexadecimal string representation of the integer.


**Example:**
```tomo
>> (255).hex(digits=4, uppercase=yes, prefix=yes)
= "0x00FF"

```
## Int.is_between

```tomo
Int.is_between : func(x: Int, low: Int, high: Int -> Bool)
```

Determines if an integer is between two numbers (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer to be checked.  | -
low | `Int` | The lower bound to check (inclusive).  | -
high | `Int` | The upper bound to check (inclusive).  | -

**Return:** `yes` if `low <= x and x <= high`, otherwise `no`


**Example:**
```tomo
>> (7).is_between(1, 10)
= yes
>> (7).is_between(100, 200)
= no
>> (7).is_between(1, 7)
= yes

```
## Int.is_prime

```tomo
Int.is_prime : func(x: Int, reps: Int = 50 -> Bool)
```

Determines if an integer is a prime number.

This function is _probabilistic_. With the default arguments, the chances of getting an incorrect answer are astronomically small (on the order of 10^(-30)). See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp) for more details.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer to be checked.  | -
reps | `Int` | The number of repetitions for primality tests.  | `50`

**Return:** `yes` if `x` is a prime number, `no` otherwise.


**Example:**
```tomo
>> (7).is_prime()
= yes
>> (6).is_prime()
= no

```
## Int.next_prime

```tomo
Int.next_prime : func(x: Int -> Int)
```

Finds the next prime number greater than the given integer.

This function is _probabilistic_, but the chances of getting an incorrect answer are astronomically small (on the order of 10^(-30)). See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp) for more details.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer after which to find the next prime.  | -

**Return:** The next prime number greater than `x`.


**Example:**
```tomo
>> (11).next_prime()
= 13

```
## Int.octal

```tomo
Int.octal : func(i: Int, digits: Int = 0, prefix: Bool = yes -> Text)
```

Converts an integer to its octal representation.

Argument | Type | Description | Default
---------|------|-------------|---------
i | `Int` | The integer to be converted.  | -
digits | `Int` | The minimum number of digits in the output string.  | `0`
prefix | `Bool` | Whether to include a "0o" prefix.  | `yes`

**Return:** The octal string representation of the integer.


**Example:**
```tomo
>> (64).octal(digits=4, prefix=yes)
= "0o0100"

```
## Int.onward

```tomo
Int.onward : func(first: Int, step: Int = 1 -> Text)
```

Return an iterator that counts infinitely from the starting integer (with an optional step size).

Argument | Type | Description | Default
---------|------|-------------|---------
first | `Int` | The starting integer.  | -
step | `Int` | The increment step size.  | `1`

**Return:** An iterator function that counts onward from the starting integer.


**Example:**
```tomo
nums : &[Int] = &[]
for i in (5).onward()
nums.insert(i)
stop if i == 10
>> nums[]
= [5, 6, 7, 8, 9, 10]

```
## Int.parse

```tomo
Int.parse : func(text: Text -> Int?)
```

Converts a text representation of an integer into an integer.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text containing the integer.  | -

**Return:** The integer represented by the text. If the given text contains a value outside of the representable range or if the entire text can't be parsed as an integer, `none` will be returned.


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
## Int.prev_prime

```tomo
Int.prev_prime : func(x: Int -> Int?)
```

Finds the previous prime number less than the given integer. If there is no previous prime number (i.e. if a number less than `2` is provided), then the function will create a runtime error.

This function is _probabilistic_, but the chances of getting an incorrect answer are astronomically small (on the order of 10^(-30)). See [the GNU MP docs](https://gmplib.org/manual/Number-Theoretic-Functions#index-mpz_005fprobab_005fprime_005fp) for more details.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer before which to find the previous prime.  | -

**Return:** The previous prime number less than `x`, or `none` if `x` is less than 2.


**Example:**
```tomo
>> (11).prev_prime()
= 7

```
## Int.sqrt

```tomo
Int.sqrt : func(x: Int -> Int)
```

Calculates the nearest square root of an integer.

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Int` | The integer whose square root is to be calculated.  | -

**Return:** The integer part of the square root of `x`.


**Example:**
```tomo
>> (16).sqrt()
= 4
>> (17).sqrt()
= 4

```
## Int.to

```tomo
Int.to : func(first: Int, last: Int, step: Int? = none -> func(->Int?))
```

Returns an iterator function that iterates over the range of numbers specified.

Argument | Type | Description | Default
---------|------|-------------|---------
first | `Int` | The starting value of the range.  | -
last | `Int` | The ending value of the range.  | -
step | `Int?` | An optional step size to use. If unspecified or `none`, the step will be inferred to be `+1` if `last >= first`, otherwise `-1`.  | `none`

**Return:** An iterator function that returns each integer in the given range (inclusive).


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
