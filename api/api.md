% API

# Builtins
## USE_COLOR

```tomo
USE_COLOR : Bool
```

Whether or not the console prefers ANSI color escape sequences in the output.

## ask

```tomo
ask : func(prompt: Text, bold: Bool = yes, force_tty: Bool = yes -> Text?)
```

Gets a line of user input text with a prompt.

When a program is receiving input from a pipe or writing its output to a pipe, this flag (which is enabled by default) forces the program to write the prompt to `/dev/tty` and read the input from `/dev/tty`, which circumvents the pipe. This means that `foo | ./tomo your-program | baz` will still show a visible prompt and read user input, despite the pipes. Setting this flag to `no` will mean that the prompt is written to `stdout` and input is read from `stdin`, even if those are pipes.

Argument | Type | Description | Default
---------|------|-------------|---------
prompt | `Text` | The text to print as a prompt before getting the input.  | -
bold | `Bool` | Whether or not to print make the prompt appear bold on a console.  | `yes`
force_tty | `Bool` | Whether or not to force the use of /dev/tty.  | `yes`

**Return:** A line of user input text without a trailing newline, or empty text if something went wrong (e.g. the user hit `Ctrl-D`).


**Example:**
```tomo
assert ask("What's your name? ") == "Arthur Dent"

```
## at_cleanup

```tomo
at_cleanup : func(fn: func() -> Void)
```

Register a function that runs at cleanup time for Tomo programs. Cleanup time happens when a program exits (see `atexit()` in C), or immediately before printing error messages in a call to `fail()`. This allows for terminal cleanup so error messages can be visible as the program shuts down.

Use this API very carefully, because errors that occur during cleanup functions may make it extremely hard to figure out what's going on. Cleanup functions should be designed to not error under any circumstances.

Argument | Type | Description | Default
---------|------|-------------|---------
fn | `func()` | A function to run at cleanup time.  | -

**Return:** Nothing.


**Example:**
```tomo
at_cleanup(func()
    (/tmp/file.txt).remove(ignore_missing=yes)
)

```
## exit

```tomo
exit : func(message: Text? = none, status: Int32 = Int32(1) -> Abort)
```

Exits the program with a given status and optionally prints a message.

Argument | Type | Description | Default
---------|------|-------------|---------
message | `Text?` | If nonempty, this message will be printed (with a newline) before exiting.  | `none`
status | `Int32` | The status code that the program with exit with.  | `Int32(1)`

**Return:** This function never returns.


**Example:**
```tomo
exit(status=1, "Goodbye forever!")

```
## fail

```tomo
fail : func(message: Text -> Abort)
```

Prints a message to the console, aborts the program, and prints a stack trace.

Argument | Type | Description | Default
---------|------|-------------|---------
message | `Text` | The error message to print.  | -

**Return:** Nothing, aborts the program.


**Example:**
```tomo
fail("Oh no!")

```
## getenv

```tomo
getenv : func(name: Text -> Text?)
```

Gets an environment variable.

Argument | Type | Description | Default
---------|------|-------------|---------
name | `Text` | The name of the environment variable to get.  | -

**Return:** If set, the environment variable's value, otherwise, `none`.


**Example:**
```tomo
assert getenv("TERM") == "xterm-256color"
assert getenv("not_a_variable") == none

```
## print

```tomo
print : func(text: Text, newline: Bool = yes -> Void)
```

Prints a message to the console (alias for say()).

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to print.  | -
newline | `Bool` | Whether or not to print a newline after the text.  | `yes`

**Return:** Nothing.


**Example:**
```tomo
print("Hello ", newline=no)
print("world!")

```
## say

```tomo
say : func(text: Text, newline: Bool = yes -> Void)
```

Prints a message to the console.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to print.  | -
newline | `Bool` | Whether or not to print a newline after the text.  | `yes`

**Return:** Nothing.


**Example:**
```tomo
say("Hello ", newline=no)
say("world!")

```
## setenv

```tomo
setenv : func(name: Text, value: Text? -> Void)
```

Sets an environment variable.

Argument | Type | Description | Default
---------|------|-------------|---------
name | `Text` | The name of the environment variable to set.  | -
value | `Text?` | The new value of the environment variable. If `none`, then the environment variable will be unset.  | -

**Return:** Nothing.


**Example:**
```tomo
setenv("FOOBAR", "xyz")

```
## sleep

```tomo
sleep : func(seconds: Num -> Void)
```

Pause execution for a given number of seconds.

Argument | Type | Description | Default
---------|------|-------------|---------
seconds | `Num` | How many seconds to sleep for.  | -

**Return:** Nothing.


**Example:**
```tomo
sleep(1.5)

```

# Bool
## Bool.parse

```tomo
Bool.parse : func(text: Text, remainder: &Text? = none -> Bool?)
```

Converts a text representation of a boolean value into a boolean. Acceptable boolean values are case-insensitive variations of `yes`/`no`, `y`/`n`, `true`/`false`, `on`/`off`.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The string containing the boolean value.  | -
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** `yes` if the string matches a recognized truthy boolean value; otherwise return `no`.


**Example:**
```tomo
assert Bool.parse("yes") == yes
assert Bool.parse("no") == no
assert Bool.parse("???") == none

assert Bool.parse("yesJUNK") == none
remainder : Text
assert Bool.parse("yesJUNK", &remainder) == yes
assert remainder == "JUNK"

```

# Byte
## Byte.get_bit

```tomo
Byte.get_bit : func(i: Byte, bit_index: Int -> Bool)
```

In the binary representation of a byte, check whether a given bit index is set to 1 or not.

The bit index must be between 1-8 or a runtime error will be produced.

Argument | Type | Description | Default
---------|------|-------------|---------
i | `Byte` | The byte whose bits are being inspected.  | -
bit_index | `Int` | The index of the bit to check (1-indexed, range 1-8).  | -

**Return:** Whether or not the given bit index is set to 1 in the byte.


**Example:**
```tomo
assert Byte(6).get_bit(1) == no
assert Byte(6).get_bit(2) == yes
assert Byte(6).get_bit(3) == yes
assert Byte(6).get_bit(4) == no

```
## Byte.hex

```tomo
Byte.hex : func(byte: Byte, uppercase: Bool = yes, prefix: Bool = no -> Text)
```

Convert a byte to a hexidecimal text representation.

Argument | Type | Description | Default
---------|------|-------------|---------
byte | `Byte` | The byte to convert to hex.  | -
uppercase | `Bool` | Whether or not to use uppercase hexidecimal letters.  | `yes`
prefix | `Bool` | Whether or not to prepend a `0x` prefix.  | `no`

**Return:** The byte as a hexidecimal text.


**Example:**
```tomo
assert Byte(18).hex() == "0x12"

```
## Byte.is_between

```tomo
Byte.is_between : func(x: Byte, low: Byte, high: Byte -> Bool)
```

Determines if an integer is between two numbers (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
x | `Byte` | The integer to be checked.  | -
low | `Byte` | The lower bound to check (inclusive).  | -
high | `Byte` | The upper bound to check (inclusive).  | -

**Return:** `yes` if `low <= x and x <= high`, otherwise `no`


**Example:**
```tomo
assert Byte(7).is_between(1, 10) == yes
assert Byte(7).is_between(100, 200) == no
assert Byte(7).is_between(1, 7) == yes

```
## Byte.parse

```tomo
Byte.parse : func(text: Text, remainder: &Text? = none -> Byte?)
```

Parse a byte literal from text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to parse.  | -
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** The byte parsed from the text, if successful, otherwise `none`.


**Example:**
```tomo
assert Byte.parse("5") == Byte(5)
assert Byte.parse("asdf") == none
assert Byte.parse("123xyz") == none

remainder : Text
assert Byte.parse("123xyz", &remainder) == Byte(123)
assert remainder == "xyz"

```
## Byte.to

```tomo
Byte.to : func(first: Byte, last: Byte, step: Byte? = none -> func(->Byte?))
```

Returns an iterator function that iterates over the range of bytes specified.

Argument | Type | Description | Default
---------|------|-------------|---------
first | `Byte` | The starting value of the range.  | -
last | `Byte` | The ending value of the range.  | -
step | `Byte?` | An optional step size to use. If unspecified or `none`, the step will be inferred to be `+1` if `last >= first`, otherwise `-1`.  | `none`

**Return:** An iterator function that returns each byte in the given range (inclusive).


**Example:**
```tomo
iter := Byte(2).to(4)
assert iter() == 2
assert iter() == 3
assert iter() == 4
assert iter() == none

assert [x for x in Byte(2).to(5)] == [Byte(2), Byte(3), Byte(4), Byte(5)]
assert [x for x in Byte(5).to(2)] == [Byte(5), Byte(4), Byte(3), Byte(2)]
assert [x for x in Byte(2).to(5, step=2)] == [Byte(2), Byte(4)]

```

# CString
## CString.as_text

```tomo
CString.as_text : func(str: CString -> Text)
```

Convert a C string to Text.

Argument | Type | Description | Default
---------|------|-------------|---------
str | `CString` | The C string.  | -

**Return:** The C string as a Text.


**Example:**
```tomo
assert CString("Hello").as_text() == "Hello"

```
## CString.join

```tomo
CString.join : func(glue: CString, pieces: [CString] -> CString)
```

Join a list of C strings together with a separator.

Argument | Type | Description | Default
---------|------|-------------|---------
glue | `CString` | The C joiner used to between elements.  | -
pieces | `[CString]` | A list of C strings to join.  | -

**Return:** A C string of the joined together bits.


**Example:**
```tomo
assert CString(",").join([CString("a"), CString("b")]) == CString("a,b")

```

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
assert (-10).abs() == 10

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
assert (4).choose(2) == 6

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
assert (2).clamped(5, 10) == 5

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
assert (10).factorial() == 3628800

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
assert (6).get_bit(1) == no
assert (6).get_bit(2) == yes
assert (6).get_bit(3) == yes
assert (6).get_bit(4) == no

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
assert (255).hex(digits=4, uppercase=yes, prefix=yes) == "0x00FF"

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
assert (7).is_between(1, 10) == yes
assert (7).is_between(100, 200) == no
assert (7).is_between(1, 7) == yes

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
assert (7).is_prime() == yes
assert (6).is_prime() == no

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
assert (11).next_prime() == 13

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
assert (64).octal(digits=4, prefix=yes) == "0o0100"

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
assert nums[] == [5, 6, 7, 8, 9, 10]

```
## Int.parse

```tomo
Int.parse : func(text: Text, base: Int? = none, remainder: &Text? = none -> Int?)
```

Converts a text representation of an integer into an integer.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text containing the integer.  | -
base | `Int?` | The numeric base to use when parsing the integer. If unspecified, the integer's base will be inferred from the text prefix. After any "+" or "-" sign, if the text begins with "0x", the base will be assumed to be 16, "0o" will assume base 8, "0b" will assume base 2, otherwise the base will be assumed to be 10.  | `none`
remainder | `&Text?` | If non-none, this argument will be set to the remainder of the text after the matching part. If none, parsing will only succeed if the entire text matches.  | `none`

**Return:** The integer represented by the text. If the given text contains a value outside of the representable range or if the entire text can't be parsed as an integer, `none` will be returned.


**Example:**
```tomo
assert Int.parse("123") == 123
assert Int.parse("0xFF") == 255
assert Int.parse("123xyz") == none
remainder : Text
assert Int.parse("123xyz", &remainder) == 123
assert remainder == "xyz"

# Can't parse:
assert Int.parse("asdf") == none

# Outside valid range:
assert Int8.parse("9999999") == none

# Explicitly specifying base:
assert Int.parse("10", base=16) == 16

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
assert (11).prev_prime() == 7

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
assert (16).sqrt() == 4
assert (17).sqrt() == 4

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
iter := (2).to(5)
assert iter() == 2
assert iter() == 3
assert iter() == 4
assert iter() == 5
assert iter() == none

assert [x for x in (2).to(5)] == [2, 3, 4, 5]
assert [x for x in (5).to(2)] == [5, 4, 3, 2]
assert [x for x in (2).to(5, step=2)] == [2, 4]

```

# List
## List.binary_search

```tomo
List.binary_search : func(list: [T], by: func(x,y:&T->Int32) = T.compare -> Int)
```

Performs a binary search on a sorted list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The sorted list to search.  | -
by | `func(x,y:&T->Int32)` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Assuming the input list is sorted according to the given comparison function, return the index where the given item would be inserted to maintain the sorted order. That is, if the item is found, return its index, otherwise return the place where it would be found if it were inserted and the list were sorted.


**Example:**
```tomo
assert [1, 3, 5, 7, 9].binary_search(5) == 3
assert [1, 3, 5, 7, 9].binary_search(-999) == 1
assert [1, 3, 5, 7, 9].binary_search(999) == 6

```
## List.by

```tomo
List.by : func(list: [T], step: Int -> [T])
```

Creates a new list with elements spaced by the specified step value.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
step | `Int` | The step value for selecting elements.  | -

**Return:** A new list with every `step`-th element from the original list.


**Example:**
```tomo
assert [1, 2, 3, 4, 5, 6].by(2) == [1, 3, 5]

```
## List.clear

```tomo
List.clear : func(list: @[T] -> Void)
```

Clears all elements from the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be cleared.  | -

**Return:** Nothing.


**Example:**
```tomo
my_list.clear()

```
## List.counts

```tomo
List.counts : func(list: [T] -> {T=Int})
```

Counts the occurrences of each element in the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to count elements in.  | -

**Return:** A table mapping each element to its count.


**Example:**
```tomo
assert [10, 20, 30, 30, 30].counts() == {10=1, 20=1, 30=3}

```
## List.find

```tomo
List.find : func(list: [T], target: T -> Int?)
```

Finds the index of the first occurrence of an element (if any).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to search through.  | -
target | `T` | The item to search for.  | -

**Return:** The index of the first occurrence or `none` if not found.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].find(20) == 2
assert [10, 20, 30, 40, 50].find(9999) == none

```
## List.from

```tomo
List.from : func(list: [T], first: Int -> [T])
```

Returns a slice of the list starting from a specified index.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
first | `Int` | The index to start from.  | -

**Return:** A new list starting from the specified index.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].from(3) == [30, 40, 50]

```
## List.has

```tomo
List.has : func(list: [T], target: T -> Bool)
```

Checks if the list has an element.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to check.  | -
target | `T` | The element to check for.  | -

**Return:** `yes` if the list has the element, `no` otherwise.


**Example:**
```tomo
assert [10, 20, 30].has(20) == yes

```
## List.heap_pop

```tomo
List.heap_pop : func(list: @[T], by: func(x,y:&T->Int32) = T.compare -> T?)
```

Removes and returns the top element of a heap or `none` if the list is empty. By default, this is the *minimum* value in the heap.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the heap.  | -
by | `func(x,y:&T->Int32)` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** The removed top element of the heap or `none` if the list is empty.


**Example:**
```tomo
my_heap := [30, 10, 20]
my_heap.heapify()
assert my_heap.heap_pop() == 10

```
## List.heap_push

```tomo
List.heap_push : func(list: @[T], item: T, by = T.compare -> Void)
```

Adds an element to the heap and maintains the heap property. By default, this is a *minimum* heap.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the heap.  | -
item | `T` | The item to be added.  | -
by | `` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Nothing.


**Example:**
```tomo
my_heap.heap_push(10)

```
## List.heapify

```tomo
List.heapify : func(list: @[T], by: func(x,y:&T->Int32) = T.compare -> Void)
```

Converts a list into a heap.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be heapified.  | -
by | `func(x,y:&T->Int32)` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Nothing.


**Example:**
```tomo
my_heap := [30, 10, 20]
my_heap.heapify()

```
## List.insert

```tomo
List.insert : func(list: @[T], item: T, at: Int = 0 -> Void)
```

Inserts an element at a specified position in the list.

Since indices are 1-indexed and negative indices mean "starting from the back", an index of `0` means "after the last item".

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
item | `T` | The item to be inserted.  | -
at | `Int` | The index at which to insert the item.  | `0`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20]
list.insert(30)
assert list == [10, 20, 30]

list.insert(999, at=2)
assert list == [10, 999, 20, 30]

```
## List.insert_all

```tomo
List.insert_all : func(list: @[T], items: [T], at: Int = 0 -> Void)
```

Inserts a list of items at a specified position in the list.

Since indices are 1-indexed and negative indices mean "starting from the back", an index of `0` means "after the last item".

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
items | `[T]` | The items to be inserted.  | -
at | `Int` | The index at which to insert the item.  | `0`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20]
list.insert_all([30, 40])
assert list == [10, 20, 30, 40]

list.insert_all([99, 100], at=2)
assert list == [10, 99, 100, 20, 30, 40]

```
## List.pop

```tomo
List.pop : func(list: &[T], index: Int = -1 -> T?)
```

Removes and returns an item from the list. If the given index is present in the list, the item at that index will be removed and the list will become one element shorter.

Since negative indices are counted from the back, the default behavior is to pop the last value.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `&[T]` | The list to remove an item from.  | -
index | `Int` | The index from which to remove the item.  | `-1`

**Return:** `none` if the list is empty or the given index does not exist in the list, otherwise the item at the given index.


**Example:**
```tomo
list := &[10, 20, 30, 40]

assert list.pop() == 40
assert list[] == [10, 20, 30]

assert list.pop(index=2) == 20
assert list[] == [10, 30]

```
## List.random

```tomo
List.random : func(list: [T], random: func(min,max:Int64->Int64)? = none -> T)
```

Selects a random element from the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list from which to select a random element.  | -
random | `func(min,max:Int64->Int64)?` | If provided, this function will be used to get a random index in the list. Returned values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** A random element from the list.


**Example:**
```tomo
assert [10, 20, 30].random() == 20

```
## List.remove_at

```tomo
List.remove_at : func(list: @[T], at: Int = -1, count: Int = 1 -> Void)
```

Removes elements from the list starting at a specified index.

Since negative indices are counted from the back, the default behavior is to remove the last item.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
at | `Int` | The index at which to start removing elements.  | `-1`
count | `Int` | The number of elements to remove.  | `1`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20, 30, 40, 50]
list.remove_at(2)
assert list == [10, 30, 40, 50]

list.remove_at(2, count=2)
assert list == [10, 50]

```
## List.remove_item

```tomo
List.remove_item : func(list: @[T], item: T, max_count: Int = -1 -> Void)
```

Removes all occurrences of a specified item from the list.

A negative `max_count` means "remove all occurrences".

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
item | `T` | The item to be removed.  | -
max_count | `Int` | The maximum number of occurrences to remove.  | `-1`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20, 10, 20, 30]
list.remove_item(10)
assert list == [20, 20, 30]

list.remove_item(20, max_count=1)
assert list == [20, 30]

```
## List.reversed

```tomo
List.reversed : func(list: [T] -> [T])
```

Returns a reversed slice of the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to be reversed.  | -

**Return:** A slice of the list with elements in reverse order.


**Example:**
```tomo
assert [10, 20, 30].reversed() == [30, 20, 10]

```
## List.sample

```tomo
List.sample : func(list: [T], count: Int, weights: [Num]? = none, random: func(->Num)? = none -> [T])
```

Selects a sample of elements from the list, optionally with weighted probabilities.

Errors will be raised if any of the following conditions occurs: - The given list has no elements and `count >= 1` - `count < 0` (negative count) - The number of weights provided doesn't match the length of the list.  - Any weight in the weights list is negative, infinite, or `NaN` - The sum of the given weights is zero (zero probability for every element).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to sample from.  | -
count | `Int` | The number of elements to sample.  | -
weights | `[Num]?` | The probability weights for each element in the list. These values do not need to add up to any particular number, they are relative weights. If no weights are given, elements will be sampled with uniform probability.  | `none`
random | `func(->Num)?` | If provided, this function will be used to get random values for sampling the list. The provided function should return random numbers between `0.0` (inclusive) and `1.0` (exclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** A list of sampled elements from the list.


**Example:**
```tomo
assert [10, 20, 30].sample(2, weights=[90%, 5%, 5%]) == [10, 10]

```
## List.shuffle

```tomo
List.shuffle : func(list: @[T], random: func(min,max:Int64->Int64)? = none -> Void)
```

Shuffles the elements of the list in place.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be shuffled.  | -
random | `func(min,max:Int64->Int64)?` | If provided, this function will be used to get a random index in the list. Returned values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** Nothing.


**Example:**
```tomo
list.shuffle()

```
## List.shuffled

```tomo
List.shuffled : func(list: [T], random: func(min,max:Int64->Int64)? = none -> [T])
```

Creates a new list with elements shuffled.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to be shuffled.  | -
random | `func(min,max:Int64->Int64)?` | If provided, this function will be used to get a random index in the list. Returned values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** A new list with shuffled elements.


**Example:**
```tomo
assert [10, 20, 30, 40].shuffled() == [40, 10, 30, 20]

```
## List.slice

```tomo
List.slice : func(list: [T], from: Int, to: Int -> [T])
```

Returns a slice of the list spanning the given indices (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
from | `Int` | The first index to include.  | -
to | `Int` | The last index to include.  | -

**Return:** A new list spanning the given indices. Note: negative indices are counted from the back of the list, so `-1` refers to the last element, `-2` the second-to-last, and so on.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].slice(2, 4) == [20, 30, 40]
assert [10, 20, 30, 40, 50].slice(-3, -2) == [30, 40]

```
## List.sort

```tomo
List.sort : func(list: @[T], by = T.compare -> Void)
```

Sorts the elements of the list in place in ascending order (small to large).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be sorted.  | -
by | `` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Nothing.


**Example:**
```tomo
list := [40, 10, -30, 20]
list.sort()
assert list == [-30, 10, 20, 40]

list.sort(func(a,b:&Int): a.abs() <> b.abs())
assert list == [10, 20, -30, 40]

```
## List.sorted

```tomo
List.sorted : func(list: [T], by = T.compare -> [T])
```

Creates a new list with elements sorted.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to be sorted.  | -
by | `` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** A new list with sorted elements.


**Example:**
```tomo
assert [40, 10, -30, 20].sorted() == [-30, 10, 20, 40]
assert [40, 10, -30, 20].sorted(
   func(a,b:&Int): a.abs() <> b.abs()
) == [10, 20, -30, 40]

```
## List.to

```tomo
List.to : func(list: [T], last: Int -> [T])
```

Returns a slice of the list from the start of the original list up to a specified index (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
last | `Int` | The index up to which elements should be included.  | -

**Return:** A new list containing elements from the start up to the specified index.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].to(3) == [10, 20, 30]
assert [10, 20, 30, 40, 50].to(-2) == [10, 20, 30, 40]

```
## List.unique

```tomo
List.unique : func(list: [T] -> {T})
```

Returns a set of the unique elements of the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to process.  | -

**Return:** A set of the unique elements from the list.


**Example:**
```tomo
assert [10, 20, 10, 10, 30].unique() == {10, 20, 30}

```
## List.where

```tomo
List.where : func(list: [T], predicate: func(item:&T -> Bool) -> Int)
```

Find the index of the first item that matches a predicate function (if any).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to search through.  | -
predicate | `func(item:&T -> Bool)` | A function that returns `yes` if the item's index should be returned or `no` if it should not.  | -

**Return:** Returns the index of the first item where the predicate is true or `none` if no item matches.


**Example:**
```tomo
assert [4, 5, 6].where(func(i:&Int): i.is_prime()) == 5
assert [4, 6, 8].find(func(i:&Int): i.is_prime()) == none

```

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
assert (-3.5).abs() == 3.5

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
assert (0.0).acos() == 1.5708

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
assert (1.0).acosh() == 0

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
assert (0.5).asin() == 0.5236

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
assert (0.0).asinh() == 0

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
assert (1.0).atan() == 0.7854

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
assert Num.atan2(1, 1) == 0.7854

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
assert (0.5).atanh() == 0.5493

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
assert (27.0).cbrt() == 3

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
assert (3.2).ceil() == 4

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
assert (2.5).clamped(5.5, 10.5) == 5.5

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
assert (3.0).copysign(-1) == -3

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
assert (0.0).cos() == 1

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
assert (0.0).cosh() == 1

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
assert (0.0).erf() == 0

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
assert (0.0).erfc() == 1

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
assert (1.0).exp() == 2.7183

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
assert (3.0).exp2() == 8

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
assert (1.0).expm1() == 1.7183

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

assert (5.0).fdim(3) == 2

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
assert (3.7).floor() == 3

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
assert Num.hypot(3, 4) == 5

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
assert (7.5).is_between(1, 10) == yes
assert (7.5).is_between(100, 200) == no
assert (7.5).is_between(1, 7.5) == yes

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
assert (1.0).isfinite() == yes
assert Num.INF.isfinite() == no

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
assert Num.INF.isinf() == yes
assert (1.0).isinf() == no

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
assert (0.0).j0() == 1

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
assert (0.0).j1() == 0

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
assert Num.E.log() == 1

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
assert (100.0).log10() == 2

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
assert (1.0).log1p() == 0.6931

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
assert (8.0).log2() == 3

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
assert (8.0).logb() == 3

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
assert (0.5).mix(10, 20) == 15
assert (0.25).mix(10, 20) == 12.5

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
assert (1.0).near(1.000000001) == yes
assert (100.0).near(110, ratio=0.1) == yes
assert (5.0).near(5.1, min_epsilon=0.1) == yes

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
assert (1.0).nextafter(1.1) == 1.0000000000000002

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
assert Num.parse("3.14") == 3.14
assert Num.parse("1e3") == 1000
assert Num.parse("1.5junk") == none
remainder : Text
assert Num.parse("1.5junk", &remainder) == 1.5
assert remainder == "junk"

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
assert (0.5).percent() == "50%"
assert (1./3.).percent(2) == "33.33%"
assert (1./3.).percent(2, precision=0.0001) == "33.3333%"
assert (1./3.).percent(2, precision=10.) == "30%"

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
assert (3.5).rint() == 4
assert (2.5).rint() == 2

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
assert (2.3).round() == 2
assert (2.7).round() == 3

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
assert (1234.567).significand() == 0.1234567

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
assert (0.0).sin() == 0

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
assert (0.0).sinh() == 0

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
assert (16.0).sqrt() == 4

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
assert (0.0).tan() == 0

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
assert (0.0).tanh() == 0

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
assert (1.0).tgamma() == 1

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
assert (3.7).trunc() == 3
assert (-3.7).trunc() == -3

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
assert (0.1234567).with_precision(0.01) == 0.12
assert (123456.).with_precision(100) == 123500
assert (1234567.).with_precision(5) == 1234565

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
assert (1.0).y0() == -0.7652

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
assert (1.0).y1() == 0.4401

```

# Path
## Path.accessed

```tomo
Path.accessed : func(path: Path, follow_symlinks: Bool = yes -> Int64?)
```

Gets the file access time of a file.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file whose access time you want.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** A 64-bit unix epoch timestamp representing when the file or directory was last accessed, or `none` if no such file or directory exists.


**Example:**
```tomo
assert (./file.txt).accessed() == 1704221100
assert (./not-a-file).accessed() == none

```
## Path.append

```tomo
Path.append : func(path: Path, text: Text, permissions: Int32 = Int32(0o644) -> Result)
```

Appends the given text to the file at the specified path, creating the file if it doesn't already exist. Failure to write will result in a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to append to.  | -
text | `Text` | The text to append to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is being created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./log.txt).append("extra line\n")!

```
## Path.append_bytes

```tomo
Path.append_bytes : func(path: Path, bytes: [Byte], permissions: Int32 = Int32(0o644) -> Result)
```

Appends the given bytes to the file at the specified path, creating the file if it doesn't already exist. Failure to write will result in a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to append to.  | -
bytes | `[Byte]` | The bytes to append to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is being created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./log.txt).append_bytes([104, 105])!

```
## Path.base_name

```tomo
Path.base_name : func(path: Path -> Text)
```

Returns the base name of the file or directory at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** The base name of the file or directory.


**Example:**
```tomo
assert (./path/to/file.txt).base_name() == "file.txt"

```
## Path.by_line

```tomo
Path.by_line : func(path: Path -> func(->Text?)?)
```

Returns an iterator that can be used to iterate over a file one line at a time, or returns none if the file could not be opened.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -

**Return:** An iterator that can be used to get lines from a file one at a time or none if the file couldn't be read.


**Example:**
```tomo
# Safely handle file not being readable:
if lines := (./file.txt).by_line()
    for line in lines
        say(line.upper())
else
    say("Couldn't read file!")

# Assume the file is readable and error if that's not the case:
for line in (/dev/stdin).by_line()!
    say(line.upper())

```
## Path.byte_writer

```tomo
Path.byte_writer : func(path: Path, append: Bool = no, permissions: Int32 = Int32(0o644) -> func(bytes:[Byte], close:Bool=no -> Result))
```

Returns a function that can be used to repeatedly write bytes to the same file.

The file writer will keep its file descriptor open after each write (unless the `close` argument is set to `yes`). If the file writer is never closed, it will be automatically closed when the file writer is garbage collected.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
append | `Bool` | If set to `yes`, writes to the file will append. If set to `no`, then the first write to the file will overwrite its contents and subsequent calls will append.  | `no`
permissions | `Int32` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Returns a function that can repeatedly write bytes to the same file. If `close` is set to `yes`, then the file will be closed after writing. If this function is called again after closing, the file will be reopened for appending.


**Example:**
```tomo
write := (./file.txt).byte_writer()
write("Hello\n".utf8())!
write("world\n".utf8(), close=yes)!

```
## Path.can_execute

```tomo
Path.can_execute : func(path: Path -> Bool)
```

Returns whether or not a file can be executed by the current user/group.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to check.  | -

**Return:** `yes` if the file or directory exists and the current user has execute permissions, otherwise `no`.


**Example:**
```tomo
assert (/bin/sh).can_execute() == yes
assert (/usr/include/stdlib.h).can_execute() == no
assert (/non/existant/file).can_execute() == no

```
## Path.can_read

```tomo
Path.can_read : func(path: Path -> Bool)
```

Returns whether or not a file can be read by the current user/group.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to check.  | -

**Return:** `yes` if the file or directory exists and the current user has read permissions, otherwise `no`.


**Example:**
```tomo
assert (/usr/include/stdlib.h).can_read() == yes
assert (/etc/shadow).can_read() == no
assert (/non/existant/file).can_read() == no

```
## Path.can_write

```tomo
Path.can_write : func(path: Path -> Bool)
```

Returns whether or not a file can be written by the current user/group.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to check.  | -

**Return:** `yes` if the file or directory exists and the current user has write permissions, otherwise `no`.


**Example:**
```tomo
assert (/tmp).can_write() == yes
assert (/etc/passwd).can_write() == no
assert (/non/existant/file).can_write() == no

```
## Path.changed

```tomo
Path.changed : func(path: Path, follow_symlinks: Bool = yes -> Int64?)
```

Gets the file change time of a file.

This is the ["ctime"](https://en.wikipedia.org/wiki/Stat_(system_call)#ctime) of a file, which is _not_ the file creation time.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file whose change time you want.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** A 64-bit unix epoch timestamp representing when the file or directory was last changed, or `none` if no such file or directory exists.


**Example:**
```tomo
assert (./file.txt).changed() == 1704221100
assert (./not-a-file).changed() == none

```
## Path.child

```tomo
Path.child : func(path: Path, child: Text -> Path)
```

Return a path that is a child of another path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of a directory.  | -
child | `Text` | The name of a child file or directory.  | -

**Return:** A new path representing the child.


**Example:**
```tomo
assert (./directory).child("file.txt") == (./directory/file.txt)

```
## Path.children

```tomo
Path.children : func(path: Path, include_hidden = no -> [Path])
```

Returns a list of children (files and directories) within the directory at the specified path. Optionally includes hidden files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `` | Whether to include hidden files, which start with a `.`.  | `no`

**Return:** A list of paths for the children.


**Example:**
```tomo
assert (./directory).children(include_hidden=yes) == [".git", "foo.txt"]

```
## Path.create_directory

```tomo
Path.create_directory : func(path: Path, permissions = Int32(0o755), recursive = yes -> Result)
```

Creates a new directory at the specified path with the given permissions. If any of the parent directories do not exist, they will be created as needed.


Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory to create.  | -
permissions | `` | The permissions to set on the new directory.  | `Int32(0o755)`
recursive | `` | If set to `yes`, then recursively create any parent directories if they don't exist, otherwise fail if the parent directory does not exist. When set to `yes`, this function behaves like `mkdir -p`.  | `yes`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./new_directory).create_directory()

```
## Path.current_dir

```tomo
Path.current_dir : func(-> Path)
```

Creates a new directory at the specified path with the given permissions. If any of the parent directories do not exist, they will be created as needed.


**Return:** The absolute path of the current directory.


**Example:**
```tomo
assert Path.current_dir() == (/home/user/tomo)

```
## Path.exists

```tomo
Path.exists : func(path: Path -> Bool)
```

Checks if a file or directory exists at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -

**Return:** `True` if the file or directory exists, `False` otherwise.


**Example:**
```tomo
assert (/).exists() == yes

```
## Path.expand_home

```tomo
Path.expand_home : func(path: Path -> Path)
```

For home-based paths (those starting with `~`), expand the path to replace the tilde with and absolute path to the user's `$HOME` directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to expand.  | -

**Return:** If the path does not start with a `~`, then return it unmodified. Otherwise, replace the `~` with an absolute path to the user's home directory.


**Example:**
```tomo
# Assume current user is 'user'
assert (~/foo).expand_home() == (/home/user/foo)
# No change
assert (/foo).expand_home() == (/foo)

```
## Path.extension

```tomo
Path.extension : func(path: Path, full: Bool = yes -> Text)
```

Returns the file extension of the file at the specified path. Optionally returns the full extension.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -
full | `Bool` | Whether to return everything after the first `.` in the base name, or only the last part of the extension.  | `yes`

**Return:** The file extension (not including the leading `.`) or an empty text if there is no file extension.


**Example:**
```tomo
assert (./file.tar.gz).extension() == "tar.gz"
assert (./file.tar.gz).extension(full=no) == "gz"
assert (/foo).extension() == ""
assert (./.git).extension() == ""

```
## Path.files

```tomo
Path.files : func(path: Path, include_hidden: Bool = no -> [Path])
```

Returns a list of files within the directory at the specified path. Optionally includes hidden files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `Bool` | Whether to include hidden files.  | `no`

**Return:** A list of file paths.


**Example:**
```tomo
assert (./directory).files(include_hidden=yes) == [(./directory/file1.txt), (./directory/file2.txt)]

```
## Path.from_components

```tomo
Path.from_components : func(components: [Text] -> Path)
```

Returns a path built from a list of path components.

Argument | Type | Description | Default
---------|------|-------------|---------
components | `[Text]` | A list of path components.  | -

**Return:** A path representing the given components.


**Example:**
```tomo
assert Path.from_components(["/", "usr", "include"]) == (/usr/include)
assert Path.from_components(["foo.txt"]) == (./foo.txt)
assert Path.from_components(["~", ".local"]) == (~/.local)

```
## Path.glob

```tomo
Path.glob : func(path: Path -> [Path])
```

Perform a globbing operation and return a list of matching paths. Some glob specific details:
- The paths "." and ".." are *not* included in any globbing results.
- Files or directories that begin with "." will not match `*`, but will match `.*`.
- Globs do support `{a,b}` syntax for matching files that match any of several
  choices of patterns.

- The shell-style syntax `**` for matching subdirectories is not supported.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory which may contain special globbing characters like `*`, `?`, or `{...}`  | -

**Return:** A list of file paths that match the glob.


**Example:**
```tomo
# Current directory includes: foo.txt, baz.txt, qux.jpg, .hidden
assert (./*).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
assert (./*.txt).glob() == [(./foo.txt), (./baz.txt)]
assert (./*.{txt,jpg}).glob() == [(./foo.txt), (./baz.txt), (./qux.jpg)]
assert (./.*).glob() == [(./.hidden)]

# Globs with no matches return an empty list:
assert (./*.xxx).glob() == []

```
## Path.group

```tomo
Path.group : func(path: Path, follow_symlinks: Bool = yes -> Text?)
```

Get the owning group of a file or directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path whose owning group to get.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** The name of the group which owns the file or directory, or `none` if the path does not exist.


**Example:**
```tomo
assert (/bin).group() == "root"
assert (/non/existent/file).group() == none

```
## Path.has_extension

```tomo
Path.has_extension : func(path: Path, extension: Text -> Bool)
```

Return whether or not a path has a given file extension.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
extension | `Text` | A file extension (leading `.` is optional). If empty, the check will test if the file does not have any file extension.  | -

**Return:** Whether or not the path has the given extension.


**Example:**
```tomo
assert (/foo.txt).has_extension("txt") == yes
assert (/foo.txt).has_extension(".txt") == yes
assert (/foo.tar.gz).has_extension("gz") == yes
assert (/foo.tar.gz).has_extension("zip") == no

```
## Path.is_directory

```tomo
Path.is_directory : func(path: Path, follow_symlinks = yes -> Bool)
```

Checks if the path represents a directory. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `` | Whether to follow symbolic links.  | `yes`

**Return:** `True` if the path is a directory, `False` otherwise.


**Example:**
```tomo
assert (./directory/).is_directory() == yes
assert (./file.txt).is_directory() == no

```
## Path.is_file

```tomo
Path.is_file : func(path: Path, follow_symlinks = yes -> Bool)
```

Checks if the path represents a file. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `` | Whether to follow symbolic links.  | `yes`

**Return:** `True` if the path is a file, `False` otherwise.


**Example:**
```tomo
assert (./file.txt).is_file() == yes
assert (./directory/).is_file() == no

```
## Path.is_socket

```tomo
Path.is_socket : func(path: Path, follow_symlinks = yes -> Bool)
```

Checks if the path represents a socket. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `` | Whether to follow symbolic links.  | `yes`

**Return:** `True` if the path is a socket, `False` otherwise.


**Example:**
```tomo
assert (./socket).is_socket() == yes

```
## Path.is_symlink

```tomo
Path.is_symlink : func(path: Path -> Bool)
```

Checks if the path represents a symbolic link.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -

**Return:** `True` if the path is a symbolic link, `False` otherwise.


**Example:**
```tomo
assert (./link).is_symlink() == yes

```
## Path.lines

```tomo
Path.lines : func(path: Path -> [Text]?)
```

Returns a list with the lines of text in a file or returns none if the file could not be opened.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -

**Return:** A list of the lines in a file or none if the file couldn't be read.


**Example:**
```tomo
lines := (./file.txt).lines()!

```
## Path.modified

```tomo
Path.modified : func(path: Path, follow_symlinks: Bool = yes -> Int64?)
```

Gets the file modification time of a file.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file whose modification time you want.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** A 64-bit unix epoch timestamp representing when the file or directory was last modified, or `none` if no such file or directory exists.


**Example:**
```tomo
assert (./file.txt).modified() == 1704221100
assert (./not-a-file).modified() == none

```
## Path.owner

```tomo
Path.owner : func(path: Path, follow_symlinks: Bool = yes -> Text?)
```

Get the owning user of a file or directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path whose owner to get.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** The name of the user who owns the file or directory, or `none` if the path does not exist.


**Example:**
```tomo
assert (/bin).owner() == "root"
assert (/non/existent/file).owner() == none

```
## Path.parent

```tomo
Path.parent : func(path: Path -> Path?)
```

Returns the parent directory of the file or directory at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** The path of the parent directory or `none` if the path is `(/)` (the file root).


**Example:**
```tomo
assert (./path/to/file.txt).parent() == (./path/to/)

```
## Path.read

```tomo
Path.read : func(path: Path -> Text?)
```

Reads the contents of the file at the specified path or none if the file could not be read.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to read.  | -

**Return:** The contents of the file. If the file could not be read, none will be returned. If the file can be read, but is not valid UTF8 data, an error will be raised.


**Example:**
```tomo
assert (./hello.txt).read() == "Hello"
assert (./nosuchfile.xxx).read() == none

```
## Path.read_bytes

```tomo
Path.read_bytes : func(path: Path, limit: Int? = none -> [Byte]?)
```

Reads the contents of the file at the specified path or none if the file could not be read.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to read.  | -
limit | `Int?` | A limit to how many bytes should be read.  | `none`

**Return:** The byte contents of the file. If the file cannot be read, none will be returned.


**Example:**
```tomo
assert (./hello.txt).read() == [72, 101, 108, 108, 111]
assert (./nosuchfile.xxx).read() == none

```
## Path.relative_to

```tomo
Path.relative_to : func(path: Path, relative_to = (./) -> Path)
```

Returns the path relative to a given base path. By default, the base path is the current directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to convert.  | -
relative_to | `` | The base path for the relative path.  | `(./)`

**Return:** A relative path from the reference point to the given path.


**Example:**
```tomo
assert (./path/to/file.txt).relative_to((./path)) == (./to/file.txt)
assert (/tmp/foo).relative_to((/tmp)) == (./foo)

```
## Path.remove

```tomo
Path.remove : func(path: Path, ignore_missing = no -> Result)
```

Removes the file or directory at the specified path. A runtime error is raised if something goes wrong.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to remove.  | -
ignore_missing | `` | Whether to ignore errors if the file or directory does not exist.  | `no`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./file.txt).remove()

```
## Path.resolved

```tomo
Path.resolved : func(path: Path, relative_to = (./) -> Path)
```

Resolves the absolute path of the given path relative to a base path. By default, the base path is the current directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to resolve.  | -
relative_to | `` | The base path for resolution.  | `(./)`

**Return:** The resolved absolute path.


**Example:**
```tomo
assert (~/foo).resolved() == (/home/user/foo)
assert (./path/to/file.txt).resolved(relative_to=(/foo)) == (/foo/path/to/file.txt)

```
## Path.set_owner

```tomo
Path.set_owner : func(path: Path, owner: Text? = none, group: Text? = none, follow_symlinks: Bool = yes -> Result)
```

Set the owning user and/or group for a path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to change the permissions for.  | -
owner | `Text?` | If non-none, the new user to assign to be the owner of the file.  | `none`
group | `Text?` | If non-none, the new group to assign to be the owner of the file.  | `none`
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./file.txt).set_owner(owner="root", group="wheel")

```
## Path.sibling

```tomo
Path.sibling : func(path: Path, name: Text -> Path)
```

Return a path that is a sibling of another path (i.e. has the same parent, but a different name). This is equivalent to `.parent().child(name)`

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
name | `Text` | The name of a sibling file or directory.  | -

**Return:** A new path representing the sibling.


**Example:**
```tomo
assert (/foo/baz).sibling("doop") == (/foo/doop)

```
## Path.subdirectories

```tomo
Path.subdirectories : func(path: Path, include_hidden = no -> [Path])
```

Returns a list of subdirectories within the directory at the specified path. Optionally includes hidden subdirectories.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `` | Whether to include hidden subdirectories.  | `no`

**Return:** A list of subdirectory paths.


**Example:**
```tomo
assert (./directory).subdirectories() == [(./directory/subdir1), (./directory/subdir2)]
assert (./directory).subdirectories(include_hidden=yes) == [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]

```
## Path.unique_directory

```tomo
Path.unique_directory : func(path: Path -> Path)
```

Generates a unique directory path based on the given path. Useful for creating temporary directories.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique directory. The last six letters of this path must be `XXXXXX`.  | -

**Return:** A unique directory path after creating the directory.


**Example:**
```tomo
assert created := (/tmp/my-dir.XXXXXX).unique_directory() == (/tmp/my-dir-AwoxbM/)
assert created.is_directory() == yes
created.remove()

```
## Path.write

```tomo
Path.write : func(path: Path, text: Text, permissions = Int32(0o644) -> Result)
```

Writes the given text to the file at the specified path, creating the file if it doesn't already exist. Sets the file permissions as specified. If the file writing cannot be successfully completed, a runtime error is raised.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
text | `Text` | The text to write to the file.  | -
permissions | `` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./file.txt).write("Hello, world!")

```
## Path.write_bytes

```tomo
Path.write_bytes : func(path: Path, bytes: [Byte], permissions = Int32(0o644) -> Result)
```

Writes the given bytes to the file at the specified path, creating the file if it doesn't already exist. Sets the file permissions as specified. If the file writing cannot be successfully completed, a runtime error is raised.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
bytes | `[Byte]` | A list of bytes to write to the file.  | -
permissions | `` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure(reason)`.


**Example:**
```tomo
(./file.txt).write_bytes([104, 105])

```
## Path.write_unique

```tomo
Path.write_unique : func(path: Path, text: Text -> Path)
```

Writes the given text to a unique file path based on the specified path. The file is created if it doesn't exist. This is useful for creating temporary files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique file. This path must include the string `XXXXXX` in the file base name.  | -
text | `Text` | The text to write to the file.  | -

**Return:** The path of the newly created unique file.


**Example:**
```tomo
created := (./file-XXXXXX.txt).write_unique("Hello, world!")
assert created == (./file-27QHtq.txt)
assert created.read() == "Hello, world!"
created.remove()

```
## Path.write_unique_bytes

```tomo
Path.write_unique_bytes : func(path: Path, bytes: [Byte] -> Path)
```

Writes the given bytes to a unique file path based on the specified path. The file is created if it doesn't exist. This is useful for creating temporary files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique file. This path must include the string `XXXXXX` in the file base name.  | -
bytes | `[Byte]` | The bytes to write to the file.  | -

**Return:** The path of the newly created unique file.


**Example:**
```tomo
created := (./file-XXXXXX.txt).write_unique_bytes([1, 2, 3])
assert created == (./file-27QHtq.txt)
assert created.read() == [1, 2, 3]
created.remove()

```
## Path.writer

```tomo
Path.writer : func(path: Path, append: Bool = no, permissions: Int32 = Int32(0o644) -> func(text:Text, close:Bool=no -> Result))
```

Returns a function that can be used to repeatedly write to the same file.

The file writer will keep its file descriptor open after each write (unless the `close` argument is set to `yes`). If the file writer is never closed, it will be automatically closed when the file writer is garbage collected.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
append | `Bool` | If set to `yes`, writes to the file will append. If set to `no`, then the first write to the file will overwrite its contents and subsequent calls will append.  | `no`
permissions | `Int32` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Returns a function that can repeatedly write to the same file. If `close` is set to `yes`, then the file will be closed after writing. If this function is called again after closing, the file will be reopened for appending.


**Example:**
```tomo
write := (./file.txt).writer()
write("Hello\n")!
write("world\n", close=yes)!

```

# Table
## Table.clear

```tomo
Table.clear : func(t: &{K:V} -> Void)
```

Removes all key-value pairs from the table.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `&{K:V}` | The reference to the table.  | -

**Return:** Nothing.


**Example:**
```tomo
t := &{"A":1}
t.clear()
assert t == {}

```
## Table.difference

```tomo
Table.difference : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a table whose key/value pairs correspond to keys only present in one table, but not the other.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table.  | -

**Return:** A table containing the common key/value pairs whose keys only appear in one table.


**Example:**
```tomo
t1 := {"A": 1; "B": 2, "C": 3}
t2 := {"B": 2, "C":30, "D": 40}
assert t1.difference(t2) == {"A": 1, "D": 40}

```
## Table.get

```tomo
Table.get : func(t: {K:V}, key: K -> V?)
```

Retrieves the value associated with a key, or returns `none` if the key is not present.

Default values for the table are ignored.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The table.  | -
key | `K` | The key whose associated value is to be retrieved.  | -

**Return:** The value associated with the key or `none` if the key is not found.


**Example:**
```tomo
t := {"A": 1, "B": 2}
assert t.get("A") == 1
assert t.get("????") == none
assert t.get("A")! == 1
assert t.get("????") or 0 == 0

```
## Table.get_or_set

```tomo
Table.get_or_set : func(t: &{K:V}, key: K, default: V -> V?)
```

If the given key is in the table, return the associated value. Otherwise, insert the given default value into the table and return it.

If no default value is provided explicitly, but the table has a default value associated with it, the table's default value will be used.
The default value is only evaluated if the key is missing.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `&{K:V}` | The table.  | -
key | `K` | The key whose associated value is to be retrieved.  | -
default | `V` | The default value to insert and return if the key is not present in the table.  | -

**Return:** Either the value associated with the key (if present) or the default value. The table will be mutated if the key is not already present.


**Example:**
```tomo
t := &{"A": @[1, 2, 3]; default=@[]}
t.get_or_set("A").insert(4)
t.get_or_set("B").insert(99)
assert t == &{"A": @[1, 2, 3, 4], "B": @[99]}

assert t.get_or_set("C", @[0, 0, 0]) == @[0, 0, 0]
assert t == &{"A": @[1, 2, 3, 4], "B": @[99], "C": @[0, 0, 0]}

```
## Table.has

```tomo
Table.has : func(t: {K:V}, key: K -> Bool)
```

Checks if the table contains a specified key.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The table.  | -
key | `K` | The key to check for presence.  | -

**Return:** `yes` if the key is present, `no` otherwise.


**Example:**
```tomo
assert {"A": 1, "B": 2}.has("A") == yes
assert {"A": 1, "B": 2}.has("xxx") == no

```
## Table.intersection

```tomo
Table.intersection : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a table with only the matching key/value pairs that are common to both tables.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table.  | -

**Return:** A table containing the common key/value pairs shared between two tables.


**Example:**
```tomo
t1 := {"A": 1; "B": 2, "C": 3}
t2 := {"B": 2, "C":30, "D": 40}
assert t1.intersection(t2) == {"B": 2}

```
## Table.remove

```tomo
Table.remove : func(t: {K:V}, key: K -> Void)
```

Removes the key-value pair associated with a specified key.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The reference to the table.  | -
key | `K` | The key of the key-value pair to remove.  | -

**Return:** Nothing.


**Example:**
```tomo
t := {"A": 1, "B": 2}
t.remove("A")
assert t == {"B": 2}

```
## Table.set

```tomo
Table.set : func(t: {K:V}, key: K, value: V -> Void)
```

Sets or updates the value associated with a specified key.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The reference to the table.  | -
key | `K` | The key to set or update.  | -
value | `V` | The value to associate with the key.  | -

**Return:** Nothing.


**Example:**
```tomo
t := {"A": 1, "B": 2}
t.set("C", 3)
assert t == {"A": 1, "B": 2, "C": 3}

```
## Table.with

```tomo
Table.with : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a copy of a table with values added from another table

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table from which new key/value pairs will be added.  | -

**Return:** The original table, but with values from the other table added.


**Example:**
```tomo
t := {"A": 1; "B": 2}
assert t.with({"B": 20, "C": 30}) == {"A": 1, "B": 20, "C": 30}

```
## Table.with_fallback

```tomo
Table.with_fallback : func(t: {K:V}, fallback: {K:V}? -> {K:V})
```

Return a copy of a table with a different fallback table.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The table whose fallback will be replaced.  | -
fallback | `{K:V}?` | The new fallback table value.  | -

**Return:** The original table with a different fallback.


**Example:**
```tomo
t := {"A": 1; fallback={"B": 2}}
t2 = t.with_fallback({"B": 3"})
assert t2["B"] == 3
t3 = t.with_fallback(none)
assert t2["B"] == none

```
## Table.without

```tomo
Table.without : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a copy of a table, but without any of the exact key/value pairs found in the other table.

Only exact key/value pairs will be discarded. Keys with a non-matching value will be kept.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table whose key/value pairs will be omitted.  | -

**Return:** The original table, but without the key/value pairs from the other table.


**Example:**
```tomo
t := {"A": 1; "B": 2, "C": 3}
assert t.without({"B": 2, "C": 30, "D": 40}) == {"A": 1, "C": 3}

```

# Text
## Text.as_c_string

```tomo
Text.as_c_string : func(text: Text -> CString)
```

Converts a `Text` value to a C-style string.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be converted to a C-style string.  | -

**Return:** A C-style string (`CString`) representing the text.


**Example:**
```tomo
assert "Hello".as_c_string() == CString("Hello")

```
## Text.at

```tomo
Text.at : func(text: Text, index: Int -> Text)
```

Get the graphical cluster at a given index. This is similar to `str[i]` with ASCII text, but has more correct behavior for unicode text.

Negative indices are counted from the back of the text, so `-1` means the last cluster, `-2` means the second-to-last, and so on.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text from which to get a cluster.  | -
index | `Int` | The index of the graphical cluster (1-indexed).  | -

**Return:** A `Text` with the single graphical cluster at the given index.


**Example:**
```tomo
assert "Amélie".at(3) == "é"

```
## Text.by_line

```tomo
Text.by_line : func(text: Text -> func(->Text?))
```

Returns an iterator function that can be used to iterate over the lines in a text.

This function ignores a trailing newline if there is one. If you don't want this behavior, use `text.by_split($/{1 nl}/)` instead.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be iterated over, line by line.  | -

**Return:** An iterator function that returns one line at a time, until it runs out and returns `none`.


**Example:**
```tomo
text := "
line one
line two
"
for line in text.by_line()
# Prints: "line one" then "line two":
say(line)

```
## Text.by_split

```tomo
Text.by_split : func(text: Text, delimiter: Text = "" -> func(->Text?))
```

Returns an iterator function that can be used to iterate over text separated by a delimiter.

To split based on a set of delimiters, use Text.by_split_any().
If an empty text is given as the delimiter, then each split will be the graphical clusters of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be iterated over in delimited chunks.  | -
delimiter | `Text` | An exact delimiter to use for splitting the text.  | `""`

**Return:** An iterator function that returns one chunk of text at a time, separated by the given delimiter, until it runs out and returns `none`.


**Example:**
```tomo
text := "one,two,three"
for chunk in text.by_split(",")
# Prints: "one" then "two" then "three":
say(chunk)

```
## Text.by_split_any

```tomo
Text.by_split_any : func(text: Text, delimiters: Text = " $\t\r\n" -> func(->Text?))
```

Returns an iterator function that can be used to iterate over text separated by one or more characters (grapheme clusters) from a given text of delimiters.

Splitting will occur on every place where one or more of the grapheme clusters in `delimiters` occurs.
To split based on an exact delimiter, use Text.by_split().

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be iterated over in delimited chunks.  | -
delimiters | `Text` | Grapheme clusters to use for splitting the text.  | `" $\t\r\n"`

**Return:** An iterator function that returns one chunk of text at a time, separated by the given delimiter characters, until it runs out and returns `none`.


**Example:**
```tomo
text := "one,two,;,three"
for chunk in text.by_split_any(",;")
# Prints: "one" then "two" then "three":
say(chunk)

```
## Text.caseless_equals

```tomo
Text.caseless_equals : func(a: Text, b: Text, language: Text = "C" -> Bool)
```

Checks whether two texts are equal, ignoring the casing of the letters (i.e. case-insensitive comparison).

Argument | Type | Description | Default
---------|------|-------------|---------
a | `Text` | The first text to compare case-insensitively.  | -
b | `Text` | The second text to compare case-insensitively.  | -
language | `Text` | The ISO 639 language code for which casing rules to use.  | `"C"`

**Return:** `yes` if `a` and `b` are equal to each other, ignoring casing, otherwise `no`.


**Example:**
```tomo
assert "A".caseless_equals("a") == yes

# Turkish lowercase "I" is "ı" (dotless I), not "i"
assert "I".caseless_equals("i", language="tr_TR") == no

```
## Text.codepoint_names

```tomo
Text.codepoint_names : func(text: Text -> [Text])
```

Returns a list of the names of each codepoint in the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text from which to extract codepoint names.  | -

**Return:** A list of codepoint names (`[Text]`).


**Example:**
```tomo
assert "Amélie".codepoint_names() == [
    "LATIN CAPITAL LETTER A",
    "LATIN SMALL LETTER M",
    "LATIN SMALL LETTER E WITH ACUTE",
    "LATIN SMALL LETTER L",
    "LATIN SMALL LETTER I",
    "LATIN SMALL LETTER E",
]

```
## Text.ends_with

```tomo
Text.ends_with : func(text: Text, suffix: Text, remainder: &Text? = none -> Bool)
```

Checks if the `Text` ends with a literal suffix text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be searched.  | -
suffix | `Text` | The literal suffix text to check for.  | -
remainder | `&Text?` | If non-none, this value will be set to the rest of the text up to the trailing suffix. If the suffix is not found, this value will be set to the original text.  | `none`

**Return:** `yes` if the text has the target, `no` otherwise.


**Example:**
```tomo
assert "hello world".ends_with("world") == yes
remainder : Text
assert "hello world".ends_with("world", &remainder) == yes
assert remainder == "hello "

```
## Text.find

```tomo
Text.find : func(text: Text, target: Text, start: Int = 1 -> Int)
```

Find a substring within a text and return its index, if found.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be searched.  | -
target | `Text` | The target text to find.  | -
start | `Int` | The index at which to begin searching.  | `1`

**Return:** The index where the first occurrence of `target` appears, or `none` if it is not found.


**Example:**
```tomo
assert "one two".find("one") == 1
assert "one two".find("two") == 5
assert "one two".find("three") == none
assert "one two".find("o", start=2) == 7

```
## Text.from

```tomo
Text.from : func(text: Text, first: Int -> Text)
```

Get a slice of the text, starting at the given position.

A negative index counts backwards from the end of the text, so `-1` refers to the last cluster, `-2` the second-to-last, etc. Slice ranges will be truncated to the length of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be sliced.  | -
first | `Int` | The index to begin the slice.  | -

**Return:** The text from the given grapheme cluster to the end of the text.


**Example:**
```tomo
assert "hello".from(2) == "ello"
assert "hello".from(-2) == "lo"

```
## Text.from_c_string

```tomo
Text.from_c_string : func(str: CString -> Text)
```

Converts a C-style string to a `Text` value.

Argument | Type | Description | Default
---------|------|-------------|---------
str | `CString` | The C-style string to be converted.  | -

**Return:** A `Text` value representing the C-style string.


**Example:**
```tomo
assert Text.from_c_string(CString("Hello")) == "Hello"

```
## Text.from_codepoint_names

```tomo
Text.from_codepoint_names : func(codepoint_names: [Text] -> [Text])
```

Returns text that has the given codepoint names (according to the Unicode specification) as its codepoints.

The text will be normalized, so the resulting text's codepoints may not exactly match the input codepoints.

Argument | Type | Description | Default
---------|------|-------------|---------
codepoint_names | `[Text]` | The names of each codepoint in the desired text (case-insentive).  | -

**Return:** A new text with the specified codepoints after normalization has been applied. Any invalid names are ignored.


**Example:**
```tomo
text := Text.from_codepoint_names([
    "LATIN CAPITAL LETTER A WITH RING ABOVE",
    "LATIN SMALL LETTER K",
    "LATIN SMALL LETTER E",
]
assert text == "Åke"

```
## Text.from_utf16

```tomo
Text.from_utf16 : func(bytes: [Int16] -> [Text])
```

Returns text that has been constructed from the given UTF16 sequence.

The text will be normalized, so the resulting text's UTF16 sequence may not exactly match the input.

Argument | Type | Description | Default
---------|------|-------------|---------
bytes | `[Int16]` | The UTF-16 integers of the desired text.  | -

**Return:** A new text based on the input UTF16 sequence after normalization has been applied.


**Example:**
```tomo
assert Text.from_utf16([197, 107, 101]) == "Åke"
assert Text.from_utf16([12371, 12435, 12395, 12385, 12399, 19990, 30028]) == "こんにちは世界".utf16()

```
## Text.from_utf32

```tomo
Text.from_utf32 : func(codepoints: [Int32] -> [Text])
```

Returns text that has been constructed from the given UTF32 codepoints.

The text will be normalized, so the resulting text's codepoints may not exactly match the input codepoints.

Argument | Type | Description | Default
---------|------|-------------|---------
codepoints | `[Int32]` | The UTF32 codepoints in the desired text.  | -

**Return:** A new text with the specified codepoints after normalization has been applied.


**Example:**
```tomo
assert Text.from_utf32([197, 107, 101]) == "Åke"

```
## Text.from_utf8

```tomo
Text.from_utf8 : func(bytes: [Byte] -> [Text])
```

Returns text that has been constructed from the given UTF8 bytes.

The text will be normalized, so the resulting text's UTF8 bytes may not exactly match the input.

Argument | Type | Description | Default
---------|------|-------------|---------
bytes | `[Byte]` | The UTF-8 bytes of the desired text.  | -

**Return:** A new text based on the input UTF8 bytes after normalization has been applied.


**Example:**
```tomo
assert Text.from_utf8([195, 133, 107, 101]) == "Åke"

```
## Text.has

```tomo
Text.has : func(text: Text, target: Text -> Bool)
```

Checks if the `Text` contains some target text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be searched.  | -
target | `Text` | The text to search for.  | -

**Return:** `yes` if the target text is found, `no` otherwise.


**Example:**
```tomo
assert "hello world".has("wo") == yes
assert "hello world".has("xxx") == no

```
## Text.join

```tomo
Text.join : func(glue: Text, pieces: [Text] -> Text)
```

Joins a list of text pieces with a specified glue.

Argument | Type | Description | Default
---------|------|-------------|---------
glue | `Text` | The text used to join the pieces.  | -
pieces | `[Text]` | The list of text pieces to be joined.  | -

**Return:** A single `Text` value with the pieces joined by the glue.


**Example:**
```tomo
assert ", ".join(["one", "two", "three"]) == "one, two, three"

```
## Text.left_pad

```tomo
Text.left_pad : func(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)
```

Pad some text on the left side so it reaches a target width.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to pad.  | -
width | `Int` | The target width.  | -
pad | `Text` | The padding text.  | `" "`
language | `Text` | The ISO 639 language code for which character width to use.  | `"C"`

**Return:** Text with length at least `width`, with extra padding on the left as needed. If `pad` has length greater than 1, it may be partially repeated to reach the exact desired length.


**Example:**
```tomo
assert "x".left_pad(5) == "    x"
assert "x".left_pad(5, "ABC") == "ABCAx"

```
## Text.lines

```tomo
Text.lines : func(text: Text -> [Text])
```

Splits the text into a list of lines of text, preserving blank lines, ignoring trailing newlines, and handling `\r\n` the same as `\n`.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be split into lines.  | -

**Return:** A list of substrings resulting from the split.


**Example:**
```tomo
assert "one\ntwo\nthree".lines() == ["one", "two", "three"]
assert "one\ntwo\nthree\n".lines() == ["one", "two", "three"]
assert "one\ntwo\nthree\n\n".lines() == ["one", "two", "three", ""]
assert "one\r\ntwo\r\nthree\r\n".lines() == ["one", "two", "three"]
assert "".lines() == []

```
## Text.lower

```tomo
Text.lower : func(text: Text, language: Text = "C" -> Text)
```

Converts all characters in the text to lowercase.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be converted to lowercase.  | -
language | `Text` | The ISO 639 language code for which casing rules to use.  | `"C"`

**Return:** The lowercase version of the text.


**Example:**
```tomo
assert "AMÉLIE".lower() == "amélie"
assert "I".lower(language="tr_TR") == "ı"

```
## Text.middle_pad

```tomo
Text.middle_pad : func(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)
```

Pad some text on the left and right side so it reaches a target width.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to pad.  | -
width | `Int` | The target width.  | -
pad | `Text` | The padding text.  | `" "`
language | `Text` | The ISO 639 language code for which character width to use.  | `"C"`

**Return:** Text with length at least `width`, with extra padding on the left and right as needed. If `pad` has length greater than 1, it may be partially repeated to reach the exact desired length.


**Example:**
```tomo
assert "x".middle_pad(6) == "  x   "
assert "x".middle_pad(10, "ABC") == "ABCAxABCAB"

```
## Text.quoted

```tomo
Text.quoted : func(text: Text, color: Bool = no, quotation_mark: Text = `"` -> Text)
```

Formats the text with quotation marks and escapes.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be quoted.  | -
color | `Bool` | Whether to add color formatting.  | `no`
quotation_mark | `Text` | The quotation mark to use.  | ``"``

**Return:** The text formatted as a quoted text.


**Example:**
```tomo
assert "one\ntwo".quoted() == "\"one\\ntwo\""

```
## Text.repeat

```tomo
Text.repeat : func(text: Text, count: Int -> Text)
```

Repeat some text multiple times.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to repeat.  | -
count | `Int` | The number of times to repeat it. (Negative numbers are equivalent to zero).  | -

**Return:** The text repeated the given number of times.


**Example:**
```tomo
assert "Abc".repeat(3) == "AbcAbcAbc"

```
## Text.replace

```tomo
Text.replace : func(text: Text, target: Text, replacement: Text -> Text)
```

Replaces occurrences of a target text with a replacement text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text in which to perform replacements.  | -
target | `Text` | The target text to be replaced.  | -
replacement | `Text` | The text to replace the target with.  | -

**Return:** The text with occurrences of the target replaced.


**Example:**
```tomo
assert "Hello world".replace("world", "there") == "Hello there"

```
## Text.reversed

```tomo
Text.reversed : func(text: Text -> Text)
```

Return a text that has the grapheme clusters in reverse order.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to reverse.  | -

**Return:** A reversed version of the text.


**Example:**
```tomo
assert "Abc".reversed() == "cbA"

```
## Text.right_pad

```tomo
Text.right_pad : func(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)
```

Pad some text on the right side so it reaches a target width.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to pad.  | -
width | `Int` | The target width.  | -
pad | `Text` | The padding text.  | `" "`
language | `Text` | The ISO 639 language code for which character width to use.  | `"C"`

**Return:** Text with length at least `width`, with extra padding on the right as needed. If `pad` has length greater than 1, it may be partially repeated to reach the exact desired length.


**Example:**
```tomo
assert "x".right_pad(5) == "x    "
assert "x".right_pad(5, "ABC") == "xABCA"

```
## Text.slice

```tomo
Text.slice : func(text: Text, from: Int = 1, to: Int = -1 -> Text)
```

Get a slice of the text.

A negative index counts backwards from the end of the text, so `-1` refers to the last cluster, `-2` the second-to-last, etc. Slice ranges will be truncated to the length of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be sliced.  | -
from | `Int` | The index of the first grapheme cluster to include (1-indexed).  | `1`
to | `Int` | The index of the last grapheme cluster to include (1-indexed).  | `-1`

**Return:** The text that spans the given grapheme cluster indices.


**Example:**
```tomo
assert "hello".slice(2, 3) == "el"
assert "hello".slice(to=-2) == "hell"
assert "hello".slice(from=2) == "ello"

```
## Text.split

```tomo
Text.split : func(text: Text, delimiter: Text = "" -> [Text])
```

Splits the text into a list of substrings based on exact matches of a delimiter.

To split based on a set of delimiters, use Text.split_any().
If an empty text is given as the delimiter, then each split will be the graphical clusters of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be split.  | -
delimiter | `Text` | The delimiter used to split the text.  | `""`

**Return:** A list of subtexts resulting from the split.


**Example:**
```tomo
assert "one,two,,three".split(",") == ["one", "two", "", "three"]
assert "abc".split() == ["a", "b", "c"]

```
## Text.split_any

```tomo
Text.split_any : func(text: Text, delimiters: Text = " $\t\r\n" -> [Text])
```

Splits the text into a list of substrings at one or more occurrences of a set of delimiter characters (grapheme clusters).

Splitting will occur on every place where one or more of the grapheme clusters in `delimiters` occurs.
To split based on an exact delimiter, use Text.split().

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be split.  | -
delimiters | `Text` | A text containing delimiters to use for splitting the text.  | `" $\t\r\n"`

**Return:** A list of subtexts resulting from the split.


**Example:**
```tomo
assert "one, two,,three".split_any(", ") == ["one", "two", "three"]

```
## Text.starts_with

```tomo
Text.starts_with : func(text: Text, prefix: Text, remainder: &Text? = none -> Bool)
```

Checks if the `Text` starts with a literal prefix text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be searched.  | -
prefix | `Text` | The literal prefix text to check for.  | -
remainder | `&Text?` | If non-none, this value will be set to the rest of the text after the prefix. If the prefix is not found, this value will be set to the original text.  | `none`

**Return:** `yes` if the text has the given prefix, `no` otherwise.


**Example:**
```tomo
assert "hello world".starts_with("hello") == yes
remainder : Text
assert "hello world".starts_with("hello", &remainder) == yes
assert remainder == " world"

```
## Text.title

```tomo
Text.title : func(text: Text, language: Text = "C" -> Text)
```

Converts the text to title case (capitalizing the first letter of each word).

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be converted to title case.  | -
language | `Text` | The ISO 639 language code for which casing rules to use.  | `"C"`

**Return:** The text in title case.


**Example:**
```tomo
assert "amélie".title() == "Amélie"

# In Turkish, uppercase "i" is "İ"
assert "i".title(language="tr_TR") == "İ"

```
## Text.to

```tomo
Text.to : func(text: Text, last: Int -> Text)
```

Get a slice of the text, ending at the given position.

A negative index counts backwards from the end of the text, so `-1` refers to the last cluster, `-2` the second-to-last, etc. Slice ranges will be truncated to the length of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be sliced.  | -
last | `Int` | The index of the last grapheme cluster to include (1-indexed).  | -

**Return:** The text up to and including the given grapheme cluster.


**Example:**
```tomo
assert "goodbye".to(3) == "goo"
assert "goodbye".to(-2) == "goodby"

```
## Text.translate

```tomo
Text.translate : func(text: Text, translations: {Text:Text} -> Text)
```

Takes a table mapping target texts to their replacements and performs all the replacements in the table on the whole text. At each position, the first matching replacement is applied and the matching moves on to *after* the replacement text, so replacement text is not recursively modified. See Text.replace() for more information about replacement behavior.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be translated.  | -
translations | `{Text:Text}` | A table mapping from target text to its replacement.  | -

**Return:** The text with all occurrences of the targets replaced with their corresponding replacement text.


**Example:**
```tomo
text := "A <tag> & an amperand".translate({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"": "&quot",
    "'": "&#39;",
})
assert text == "A &lt;tag&gt; &amp; an ampersand"

```
## Text.trim

```tomo
Text.trim : func(text: Text, to_trim: Text = " $\t\r\n", left: Bool = yes, right: Bool = yes -> Text)
```

Trims the given characters (grapheme clusters) from the left and/or right side of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be trimmed.  | -
to_trim | `Text` | The characters to remove from the left/right of the text.  | `" $\t\r\n"`
left | `Bool` | Whether or not to trim from the front of the text.  | `yes`
right | `Bool` | Whether or not to trim from the back of the text.  | `yes`

**Return:** The text without the trim characters at either end.


**Example:**
```tomo
assert "   x y z    \n".trim() == "x y z"
assert "one,".trim(",") == "one"
assert "   xyz   ".trim(right=no) == "xyz   "

```
## Text.upper

```tomo
Text.upper : func(text: Text, language: Text = "C" -> Text)
```

Converts all characters in the text to uppercase.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be converted to uppercase.  | -
language | `Text` | The ISO 639 language code for which casing rules to use.  | `"C"`

**Return:** The uppercase version of the text.


**Example:**
```tomo
assert "amélie".upper() == "AMÉLIE"

# In Turkish, uppercase "i" is "İ"
assert "i".upper(language="tr_TR") == "İ"

```
## Text.utf16

```tomo
Text.utf16 : func(text: Text -> [Int16])
```

Returns a list of Unicode code points for UTF16 encoding of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text from which to extract Unicode code points.  | -

**Return:** A list of 16-bit integer Unicode code points (`[Int16]`).


**Example:**
```tomo
assert "Åke".utf16() == [197, 107, 101]
assert "こんにちは世界".utf16() == [12371, 12435, 12395, 12385, 12399, 19990, 30028]

```
## Text.utf32

```tomo
Text.utf32 : func(text: Text -> [Int32])
```

Returns a list of Unicode code points for UTF32 encoding of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text from which to extract Unicode code points.  | -

**Return:** A list of 32-bit integer Unicode code points (`[Int32]`).


**Example:**
```tomo
assert "Amélie".utf32() == [65, 109, 233, 108, 105, 101]

```
## Text.utf8

```tomo
Text.utf8 : func(text: Text -> [Byte])
```

Converts a `Text` value to a list of bytes representing a UTF8 encoding of the text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be converted to UTF8 bytes.  | -

**Return:** A list of bytes (`[Byte]`) representing the text in UTF8 encoding.


**Example:**
```tomo
assert "Amélie".utf8() == [65, 109, 195, 169, 108, 105, 101]

```
## Text.width

```tomo
Text.width : func(text: Text -> Int)
```

Returns the display width of the text as seen in a terminal with appropriate font rendering. This is usually the same as the text's `.length`, but there are some characters like emojis that render wider than 1 cell.

This will not always be exactly accurate when your terminal's font rendering can't handle some unicode displaying correctly.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text whose length you want.  | -

**Return:** An integer representing the display width of the text.


**Example:**
```tomo
assert "Amélie".width() == 6
assert "🤠".width() == 2

```
## Text.without_prefix

```tomo
Text.without_prefix : func(text: Text, prefix: Text -> Text)
```

Returns the text with a given prefix removed (if present).

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to remove the prefix from.  | -
prefix | `Text` | The prefix to remove.  | -

**Return:** A text without the given prefix (if present) or the unmodified text if the prefix is not present.


**Example:**
```tomo
assert "foo:baz".without_prefix("foo:") == "baz"
assert "qux".without_prefix("foo:") == "qux"

```
## Text.without_suffix

```tomo
Text.without_suffix : func(text: Text, suffix: Text -> Text)
```

Returns the text with a given suffix removed (if present).

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to remove the suffix from.  | -
suffix | `Text` | The suffix to remove.  | -

**Return:** A text without the given suffix (if present) or the unmodified text if the suffix is not present.


**Example:**
```tomo
assert "baz.foo".without_suffix(".foo") == "baz"
assert "qux".without_suffix(".foo") == "qux"

```
