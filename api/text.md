# Text

`Text` is Tomo's datatype to represent text. The name `Text` is used instead of
"string" because Tomo represents text as an immutable UTF-8-encoded value that
uses the Boehm Cord library for efficient storage and concatenation. These are
_not_ C-style NULL-terminated character arrays. GNU libunistring is used for
full Unicode functionality (grapheme cluster counts, capitalization, etc.).

## Syntax

Text has a flexible syntax designed to make it easy to hold values from
different languages without the need to have lots of escape sequences and
without using printf-style string formatting.

```
// Basic text:
str := "Hello world"
str2 := 'Also text'
str3 := `Backticks too`
```

## Line Splits

Long text can be split across multiple lines by having two or more dots at the
start of a new line on the same indentation level that started the text:

```
str := "This is a long
....... line that is split in code"
```

## Multi-line Text

Multi-line text has indented (i.e. at least one tab more than the start of the
text) text inside quotation marks. The leading and trailing newline are
ignored:

```
multi_line := "
    This text has multiple lines.
    Line two.

    You can split a line
.... using two or more dots to make an elipsis.

    Remember to include whitespace after the elipsis if desired.

    Or don't if you're splitting a long word like supercalifragilisticexpia
....lidocious

        This text is indented by one level in the text

    "quotes" are ignored unless they're at the same indentation level as the
.... start of the text.

    The end (no newline after this).
"
```

## Text Interpolations

Inside double quoted text, you can use a dollar sign (`$`) to insert an
expression that you want converted to text. This is called text interpolation:

```
// Interpolation:
my_var := 5
str := "My var is $my_var!"
// Equivalent to "My var is 5!"

// Using parentheses:
str := "Sum: $(1 + 2)"
// equivalent to "Sum: 3"
```

Single-quoted text does not have interpolations:

```
// No interpolation here:
str := 'Sum: $(1 + 2)'
```

## Text Escapes

Unlike other languages, backslash is *not* a special character inside of text.
For example, `"x\ny"` has the characters `x`, `\`, `n`, `y`, not a newline.
Instead, a series of character escapes act as complete text literals without
quotation marks:

```
newline := \n
crlf := \r\n
quote := \"
```

These text literals can be used as interpolation values with or without
parentheses, depending on which you find more readable:

```
two_lines := "one$(\n)two"
has_quotes := "some $\"quotes$\" here"
```

However, in general it is best practice to use multi-line text to avoid these problems:

```
str := "
    This has
    multiple lines and "quotes" too!
"
```

### Multi-line Text

There are two reasons for text to span multiple lines in code: either you
have text that contains newlines and you want to represent it without `\n`
escapes, or you have a long single-line text that you want to split across
multiple lines for readability. To support this, you can use newlines inside of
text with indentation-sensitivity. For splitting long lines, use two or more
"."s at the same indentation level as the start of the text literal:

```
single_line := "This is a long text that
... spans multiple lines"
```
For text that contains newlines, you may put multiple indented lines inside
the quotes:

```
multi_line := "
    line one
    line two
        this line is indented
    last line
"
```

Text may only end on lines with the same indentation as the starting quote
and nested quotes are ignored:

```
multi_line := "
    Quotes in indented regions like this: " don't count
"
```

If there is a leading or trailing newline, it is ignored and not included in
the text.

```
str := "
    one line
"

>>> str == "one line"
=== yes
```

Additional newlines *are* counted though:

```
str := "
    
    blank lines

"

>>> str == "{\n}blank lines{\n}"
```

### Customizable `$`-Text

Sometimes you might need to use a lot of literal `$`s or quotation marks in
text. In such cases, you can use the more customizable form of text. The
customizable form lets you explicitly specify which character to use for
interpolation and which characters to use for delimiting the text.

The first character after the `$` is the custom interpolation character, which
can be any of the following symbols: `~!@#$%^&*+=\?`. If none of these
characters is used, the default interpolation character is `$`. Since this is
the default, you can disable interpolation altogether by using `$` here (i.e. a
double `$$`).

The next thing in a customizable text is the character used to delimit the
text. The text delimiter can be any of the following symbols: `` "'`|/;([{< ``
If the text delimiter is one of `([{<`, then the text will continue until a
matching `)]}>` is found, not terminating unless the delimiters are balanced
(i.e. nested pairs of delimiters are considered part of the text).

Here are some examples:

```
$"Equivalent to normal text with dollar interps: $(1 + 2)"
$@"The same, but the AT symbol interpolates: @(1 + 2)"
$$"No interpolation here, $ is just a literal character"
$|This text is pipe-delimited, so it can have "quotes" and 'single quotes' and interpolates with dollar sign: $(1+2)|
$(This text is parens-delimited, so you can have (nested) parens without ending the text)
$=[This text is square-bracket delimited [which can be nested] and uses equals for interps: =(1 + 2)]
$@/look ma, regex literals!/
```

When text is delimited by matching pairs (`()`, `[]`, `{}`, or `<>`), they
can only be closed by a matched closing character at the same indentation
level, ignoring nested pairs:

```
$$(Inside parens, you can have (nested ()) parens no problem)
$$"But only (), [], {}, and <> are matching pairs, you can't have nested quotes"
$$(
    When indented, an unmatched ) won't close the text
    An unmatched ( won't mess things up either
    Only matching pairs on the same indentation level are counted:
)
$$(Multi-line text with nested (parens) and
.. line continuation)
```

As a special case, when you use the same character for interpolation and text
delimiting, no interpolations are allowed:

```
plain := $""This text has {no interpolations}!"
```

**Note:** Normal doubly quoted text with no dollar sign (e.g. `"foo"`) are a
shorthand for `${}"foo"`. Singly quoted text with no dollar sign (e.g.
`'foo'`) are shorthand for `$''foo'`.

## Operations

### Concatenation

Concatenation in the typical case is an O(1) operation: `"{x}{y}"` or `x ++ y`.

Because text concatenation is typically an O(1) operation, there is no need for
a separate "string builder" class in the language and no need to use an array
of text fragments.

### Text Length

Text length is an ambiguous term in the context of UTF-8 text. There are
several possible meanings, so each of these meanings is split into a separate
method:

- Number of grapheme clusters: `text:num_clusters()`. This is probably what
  you want to use, since it corresponds to the everyday notion of "letters".
- Size in bytes: `text:num_bytes()`
- Number of unicode codepoints: `text:num_codepoints()` (you probably want to
  use clusters, not codepoints in most applications)

Since the typical user expectation is that text length refers to "letters,"
the `#` length operator returns the number of grapheme clusters, which is the
closest unicode equivalent to "letters."

### Iteration

Iteration is *not* supported for text because of the ambiguity between bytes,
codepoints, and grapheme clusters. It is instead recommended that you
explicitly iterate over bytes, codepoints, graphemes, words, lines, etc:

### Equality, Comparison, and Hashing

All text is compared and hashed using unicode normalization. Unicode provides
several different ways to represent the same text. For example, the single
codepoint `U+E9` (latin small e with accent) is rendered the same as the two
code points `U+65 U+301` (latin small e, acute combining accent) and has an
equivalent linguistic meaning. These are simply different ways to represent the
same "letter." In order to make it easy to write correct code that takes this
into account, Tomo uses unicode normalization for all text comparisons and
hashing. Normalization does the equivalent of converting text to a canonical
form before performing comparisons or hashing. This means that if a table is
created that has text with the codepoint `U+E9` as a key, then a lookup with
the same text but with `U+65 U+301` instead of `U+E9` will still succeed in
finding the value because the two texts are equivalent under normalization.


# Text Functions

## `as_c_string`

**Description:**  
Converts a `Text` value to a C-style string.

**Usage:**  
```tomo
as_c_string(text: Text) -> CString
```

**Parameters:**

- `text`: The text to be converted to a C-style string.

**Returns:**  
A C-style string (`CString`) representing the text.

**Example:**  
```tomo
>> "Hello":as_c_string()
= CString("Hello")
```

---

## `bytes`

**Description:**  
Converts a `Text` value to an array of bytes.

**Usage:**  
```tomo
bytes(text: Text) -> [Int8]
```

**Parameters:**

- `text`: The text to be converted to bytes.

**Returns:**  
An array of bytes (`[Int8]`) representing the text.

**Example:**  
```tomo
>> "Amélie":bytes()
= [65_i8, 109_i8, 101_i8, -52_i8, -127_i8, 108_i8, 105_i8, 101_i8]
```

---

## `character_names`

**Description:**  
Returns a list of character names from the text.

**Usage:**  
```tomo
character_names(text: Text) -> [Text]
```

**Parameters:**

- `text`: The text from which to extract character names.

**Returns:**  
A list of character names (`[Text]`).

**Example:**  
```tomo
>> "Amélie":character_names()
= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E", "COMBINING ACUTE ACCENT", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
```

---

## `clusters`

**Description:**  
Breaks the text into a list of unicode graphical clusters. Clusters are what
you typically think of when you think of "letters" or "characters". If you're
in a text editor and you hit the left or right arrow key, it will move the
cursor by one graphical cluster.

**Usage:**  
```tomo
clusters(text: Text) -> [Text]
```

**Parameters:**

- `text`: The text to be broken into graphical clusters.

**Returns:**  
A list of graphical clusters (`[Text]`) within the text.

**Example:**  
```tomo
>> "Amélie":clusters()
= ["A", "m", "é", "l", "i", "e"] : [Text]
```

---

## `codepoints`

**Description:**  
Returns a list of Unicode code points for the text.

**Usage:**  
```tomo
codepoints(text: Text) -> [Int32]
```

**Parameters:**

- `text`: The text from which to extract Unicode code points.

**Returns:**  
A list of Unicode code points (`[Int32]`).

**Example:**  
```tomo
>> "Amélie":codepoints()
= [65_i32, 109_i32, 101_i32, 769_i32, 108_i32, 105_i32, 101_i32] : [Int32]
```

---

## `from_c_string`

**Description:**  
Converts a C-style string to a `Text` value.

**Usage:**  
```tomo
from_c_string(str: CString) -> Text
```

**Parameters:**

- `str`: The C-style string to be converted.

**Returns:**  
A `Text` value representing the C-style string.

**Example:**  
```tomo
from_c_string(CString("Hello"))  // "Hello"
```

---

## `has`

**Description:**  
Checks if the `Text` contains a target substring.

**Usage:**  
```tomo
has(text: Text, target: Text, where: Where = Where.Anywhere) -> Bool
```

**Parameters:**

- `text`: The text to be searched.
- `target`: The substring to search for.
- `where`: The location to search (`Where.Anywhere` by default).

**Returns:**  
`yes` if the target substring is found, `no` otherwise.

**Example:**  
```tomo
has("Hello, world!", "world")  // yes
```

---

## `join`

**Description:**  
Joins a list of text pieces with a specified glue.

**Usage:**  
```tomo
join(glue: Text, pieces: [Text]) -> Text
```

**Parameters:**

- `glue`: The text used to join the pieces.
- `pieces`: The list of text pieces to be joined.

**Returns:**  
A single `Text` value with the pieces joined by the glue.

**Example:**  
```tomo
join(", ", ["apple", "banana", "cherry"])  // "apple, banana, cherry"
```

---

## `lower`

**Description:**  
Converts all characters in the text to lowercase.

**Usage:**  
```tomo
lower(text: Text) -> Text
```

**Parameters:**

- `text`: The text to be converted to lowercase.

**Returns:**  
The lowercase version of the text.

**Example:**  
```tomo
lower("HELLO")  // "hello"
```

---

## `num_bytes`

**Description:**  
Returns the number of bytes used by the text.

**Usage:**  
```tomo
num_bytes(text: Text) -> Int
```

**Parameters:**

- `text`: The text to measure.

**Returns:**  
The number of bytes used by the text.

**Example:**  
```tomo
num_bytes("Hello")  // 5
```

---

## `num_clusters`

**Description:**  
Returns the number of clusters in the text.

**Usage:**  
```tomo
num_clusters(text: Text) -> Int
```

**Parameters:**

- `text`: The text to measure.

**Returns:**  
The number of clusters in the text.

**Example:**  
```tomo
num_clusters("Hello")  // 5
```

---

## `num_codepoints`

**Description:**  
Returns the number of Unicode code points in the text.

**Usage:**  
```tomo
num_codepoints(text: Text) -> Int
```

**Parameters:**

- `text`: The text to measure.

**Returns:**  
The number of Unicode code points in the text.

**Example:**  
```tomo
num_codepoints("Hello")  // 5
```

---

## `quoted`

**Description:**  
Formats the text as a quoted string.

**Usage:**  
```tomo
quoted(text: Text, color: Bool = no) -> Text
```

**Parameters:**

- `text`: The text to be quoted.
- `color`: Whether to add color formatting (default is `no`).

**Returns:**  
The text formatted as a quoted string.

**Example:**  
```tomo
quoted("Hello")  // "\"Hello\""
```

---

## `replace`

**Description:**  
Replaces occurrences of a pattern in the text with a replacement string.

**Usage:**  
```tomo
replace(text: Text, pattern: Text, replacement: Text, limit: Int = -1) -> Text
```

**Parameters:**

- `text`: The text in which to perform replacements.
- `pattern`: The substring to be replaced.
- `replacement`: The text to replace the pattern with.
- `limit`: The maximum number of replacements (default is `-1`, meaning no limit).

**Returns:**  
The text with occurrences of the pattern replaced.

**Example:**  
```tomo
replace("Hello world", "world", "there")  // "Hello there"
```

---

## `split`

**Description:**  
Splits the text into a list of substrings based on a delimiter.

**Usage:**  
```tomo
split(text: Text, split: Text) -> [Text]
```

**Parameters:**

- `text`: The text to be split.
- `split`: The delimiter used to split the text.

**Returns:**  
A list of substrings resulting from the split.

**Example:**  
```tomo
split("apple,banana,cherry", ",")  // ["apple", "banana", "cherry"]
```

---

## `title`

**Description:**  
Converts the text to title case (capitalizing the first letter of each word).

**Usage:**  
```tomo
title(text: Text) -> Text
```

**Parameters:**

- `text`: The text to be converted to title case.

**Returns:**  
The text in title case.

**Example:**  
```tomo
title("hello world")  // "Hello World"
```

---

## `trimmed`

**Description:**  
Trims characters from the beginning and end of the text.

**Usage:**  
```tomo
trimmed(text: Text, trim: Text = " {\n\r\t}", where: Where = Where.Anywhere) -> Text
```

**Parameters:**

- `text`: The text to be trimmed.
- `trim`: The set of characters to remove (default is `" {\n\r\t}"`).
- `where`: Specifies where to trim (`Where.Anywhere` by default).

**Returns:**  
The trimmed text.

**Example:**  
```tomo
trimmed("  Hello  ")  // "Hello"
```

---

## `upper`

**Description:**  
Converts all characters in the text to uppercase.

**Usage:**  
```tomo
upper(text: Text) -> Text
```

**Parameters:**

- `text`: The text to be converted to uppercase.

**Returns:**  
The uppercase version of the text.

**Example:**  
```tomo
upper("hello")  // "HELLO"
```

---

## `without`

**Description:**  
Removes all occurrences of a target substring from the text.

**Usage:**  
```tomo
without(text: Text, target: Text, where: Where = Where.Anywhere) -> Text
```

**Parameters:**

- `text`: The text from which to remove substrings.
- `target`: The substring to remove.
- `where`: The location to remove the target (`Where.Anywhere` by default).

**Returns:**  
The text with occurrences of the target removed.

**Example:**  
```tomo
without("Hello world", "world")  // "Hello "
```

---
