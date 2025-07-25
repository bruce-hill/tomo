% API

# Builtins

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
>> Byte(6).get_bit(1)
= no
>> Byte(6).get_bit(2)
= yes
>> Byte(6).get_bit(3)
= yes
>> Byte(6).get_bit(4)
= no

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
>> Byte(18).hex()
= "0x12"

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
>> Byte(7).is_between(1, 10)
= yes
>> Byte(7).is_between(100, 200)
= no
>> Byte(7).is_between(1, 7)
= yes

```
## Byte.parse

```tomo
Byte.parse : func(text: Text -> Byte?)
```

Parse a byte literal from text.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to parse.  | -

**Return:** The byte parsed from the text, if successful, otherwise `none`.


**Example:**
```tomo
>> Byte.parse("5")
= Byte(5)?
>> Byte.parse("asdf")
= none

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
>> Byte(2).to(5)
= func(->Byte?)
>> [x for x in Byte(2).to(5)]
= [Byte(2), Byte(3), Byte(4), Byte(5)]
>> [x for x in Byte(5).to(2)]
= [Byte(5), Byte(4), Byte(3), Byte(2)]

>> [x for x in Byte(2).to(5, step=2)]
= [Byte(2), Byte(4)]

```
