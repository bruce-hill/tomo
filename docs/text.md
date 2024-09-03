# Text

`Text` is Tomo's datatype to represent text. The name `Text` is used instead of
"string" because Tomo text represents immutable, normalized unicode data with
fast indexing that has an implementation that is efficient for concatenation.
These are _not_ C-style NULL-terminated character arrays. GNU libunistring is
used for full Unicode functionality (grapheme cluster counts, capitalization,
etc.).

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

Text length gives you the number of grapheme clusters in the text, according to
the unicode standard. This corresponds to what you would intuitively think of
when you think of "letters" in a string. If you have text with an emoji that has
several joining modifiers attached to it, that text has a length of 1.

```tomo
>> "hello".length
= 5
>> "👩🏽‍🚀".length
= 1
```

### Iteration

Iteration is *not* supported for text. It is rarely ever the case that you will
need to iterate over text, but if you do, you can iterate over the length of
the text and retrieve 1-wide slices. Alternatively, you can split the text into
its constituent grapheme clusters with `text:split()` and iterate over those.

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

# Patterns

As an alternative to full regular expressions, Tomo provides a limited string
matching pattern syntax that is intended to solve 80% of use cases in 1% of the
code size (PCRE's codebase is roughly 150k lines of code, and Tomo's entire
Text codebase is around 1.5K lines of code).

For more advanced use cases, consider linking against a C library for regular
expressions or pattern matching.

Patterns are used in a small, but very powerful API that handles many text
functions that would normally be handled by a more extensive API:

```
Text.find(pattern:Text, start=1, length=!&Int64?)->Int
Text.find_all(pattern:Text)->[Text]
Text.split(pattern:Text)->[Text]
Text.replace(pattern:Text, replacement:Text)->[Text]
Text.has(pattern:Text, where=Where.Anywhere)->Bool
```

See [Text Functions](#Text-Functions) for the full API documentation.

## Syntax

Patterns have three types of syntax:

- `[..` followed by an optional count (`n`, `n-m`, or `n+`), followed by an
  optional `!` to negate the pattern, followed by an optional pattern name or
  Unicode character name, followed by a required `]`.

- Any matching pair of quotes or parentheses or braces with a `?` in the middle
  (e.g. `"?"` or `(?)`).

- Any other character is treated as a literal to be matched exactly.

## Named Patterns

Named patterns match certain pre-defined patterns that are commonly useful. To
use a named pattern, use the syntax `[..name]`. Names are case-insensitive and
mostly ignore spaces, underscores, and dashes.

- ` ` - If no name is given, any character is accepted.
- `digit` - A unicode digit
- `email` - an email address
- `emoji` - an emoji
- `end` - the very end of the text
- `id` - A unicode identifier
- `int` - One or more digits with an optional `-` (minus sign) in front
- `ip` - an IP address (IPv4 or IPv6)
- `ipv4` - an IPv4 address
- `ipv6` - an IPv6 address
- `num` - One or more digits with an optional `-` (minus sign) in front and an optional `.` and more digits after
- `start` - the very start of the text
- `uri` - a URI
- `url` - a URL (URI that specifically starts with `http://`, `https://`, `ws://`, `wss://`, or `ftp://`)

For non-alphabetic characters, any single character is treated as matching
exactly that character. For example, `[..1 []` matches exactly one `[`
character. Or, `[..1 (]` matches exactly one `(` character.

Patterns can also use any Unicode property name. Some helpful ones are:

- `hex` - Hexidecimal digits
- `lower` - Lowercase letters
- `space` - The space character
- `upper` - Uppercase letters
- `whitespace` - Whitespace characters

Patterns may also use exact Unicode codepoint names. For example: `[..1 latin
small letter A]` matches `a`.

## Negating Patterns

If an exclamation mark (`!`) is placed before a pattern's name, then characters
are matched only when they _don't_ match the pattern. For example, `[..!alpha]`
will match all characters _except_ alphabetic ones.

## Repetitions

By default, named patterns match 1 or more repetitions, but you can specify how
many repetitions you want by putting a number or range of numbers first using
`n` (exactly `n` repetitions), `n-m` (between `n` and `m` repetitions), or `n+`
(`n` or more repetitions):

```
[..4-5 alpha]
0x[..hex]
[..4 digit]-[..2 digit]-[..2 digit]
[..2+ space]
[..0-1 question mark]
```

## Some Examples

URL query string parameters:

```
text := "example.com/page?a=b&c=d"
>> text:find(before=$Pat`?`, $Pat`[..]`):split($Pat`&`)
= ["a=b", "c=d"]
```

Remove or get file extension:

```
filename := "foo.txt"
>> filename:without($Pat`.[:id:]`, where=End)
= "foo"

>> filename:find(before=$Pat`.`, $Pat`[:id:][:end:]`)
= MatchResult.Success(match="txt")

>> filename := "foo.tar.gz"
>> ".":join(filename:split($Pat`.`):from(2))
= "tar.gz"
```

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

## `utf8_bytes`

**Description:**  
Converts a `Text` value to an array of bytes representing a UTF8 encoding of
the text.

**Usage:**  
```tomo
bytes(text: Text) -> [Int8]
```

**Parameters:**

- `text`: The text to be converted to UTF8 bytes.

**Returns:**  
An array of bytes (`[Int8]`) representing the text in UTF8 encoding.

**Example:**  
```tomo
>> "Amélie":utf8_bytes()
= [65_i8, 109_i8, -61_i8, -87_i8, 108_i8, 105_i8, 101_i8] : [Int8]
```

---

## `codepoint_names`

**Description:**  
Returns an array of the names of each codepoint in the text.

**Usage:**  
```tomo
codepoint_names(text: Text) -> [Text]
```

**Parameters:**

- `text`: The text from which to extract codepoint names.

**Returns:**  
An array of codepoint names (`[Text]`).

**Example:**  
```tomo
>> "Amélie":codepoint_names()
= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
```

---

## `utf32_codepoints`

**Description:**  
Returns an array of Unicode code points for UTF32 encoding of the text.

**Usage:**  
```tomo
utf32_codepoints(text: Text) -> [Int32]
```

**Parameters:**

- `text`: The text from which to extract Unicode code points.

**Returns:**  
An array of 32-bit integer Unicode code points (`[Int32]`).

**Example:**  
```tomo
>> "Amélie":utf32_codepoints()
= [65_i32, 109_i32, 233_i32, 108_i32, 105_i32, 101_i32] : [Int32]
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
>> Text.from_c_string(CString("Hello"))
= "Hello"
```

---

## `from_codepoint_names`

**Description:**  
Returns text that has the given codepoint names (according to the Unicode
specification) as its codepoints. Note: the text will be normalized, so the
resulting text's codepoints may not exactly match the input codepoints.

**Usage:**  
```tomo
from_codepoint_names(codepoint_names: [Text]) -> [Text]
```

**Parameters:**

- `codepoint_names`: The names of each codepoint in the desired text. Names
  are case-insentive.

**Returns:**  
A new text with the specified codepoints after normalization has been applied.
Any invalid names are ignored.

**Example:**  
```tomo
>> Text.from_codepoint_names([
  "LATIN CAPITAL LETTER A WITH RING ABOVE",
  "LATIN SMALL LETTER K",
  "LATIN SMALL LETTER E",
]
= "Åke"
```

---

## `from_codepoints`

**Description:**  
Returns text that has been constructed from the given UTF32 codepoints. Note:
the text will be normalized, so the resulting text's codepoints may not exactly
match the input codepoints.

**Usage:**  
```tomo
from_codepoint_names(codepoints: [Int32]) -> [Text]
```

**Parameters:**

- `codepoints`: The UTF32 codepoints in the desired text.

**Returns:**  
A new text with the specified codepoints after normalization has been applied.

**Example:**  
```tomo
>> Text.from_codepoints([197_i32, 107_i32, 101_i32])
= "Åke"
```

---

## `from_bytes`

**Description:**  
Returns text that has been constructed from the given UTF8 bytes. Note: the
text will be normalized, so the resulting text's UTF8 bytes may not exactly
match the input.

**Usage:**  
```tomo
from_codepoint_names(codepoints: [Int32]) -> [Text]
```

**Parameters:**

- `codepoints`: The UTF32 codepoints in the desired text.

**Returns:**  
A new text based on the input UTF8 bytes after normalization has been applied.

**Example:**  
```tomo
>> Text.from_bytes([-61_i8, -123_i8, 107_i8, 101_i8])
= "Åke"
```

---

## `find`

**Description:**  
Finds the first occurrence of a pattern in the given text (if any).
See: [Patterns](#Patterns) for more information on patterns.

**Usage:**  
```tomo
find(text: Text, pattern: Text, start: Int = 1, length: &Int64? = !&Int64) -> Int
```

**Parameters:**

- `text`: The text to be searched.
- `pattern`: The pattern to search for.
- `start`: The index to start the search.
- `length`: If non-null, this pointer's value will be set to the length of the
  match, or `-1` if there is no match.

**Returns:**  
`0` if the target pattern is not found, otherwise the index where the match was
found.

**Example:**  
```tomo
>> " one   two  three   ":find("[..id]", start=-999)
= 0
>> " one   two  three   ":find("[..id]", start=999)
= 0
>> " one   two  three   ":find("[..id]")
= 2
>> " one   two  three   ":find("[..id]", start=5)
= 8

>> len := 0_i64
>> "   one  ":find("[..id]", length=&len)
= 4
>> len
= 3_i64
```

---

## `find_all`

**Description:**  
Finds all occurrences of a pattern in the given text.
See: [Patterns](#Patterns) for more information on patterns.

**Usage:**  
```tomo
find_all(text: Text, pattern: Text) -> [Text]
```

**Parameters:**

- `text`: The text to be searched.
- `pattern`: The pattern to search for.

**Returns:**  
An array of every match of the pattern in the given text.
Note: if `text` or `pattern` is empty, an empty array will be returned.

**Example:**  
```tomo
>> " one  two three   ":find_all("[..alpha]")
= ["one", "two", "three"]

>> " one  two three   ":find_all("[..!space]")
= ["one", "two", "three"]

>> "    ":find_all("[..alpha]")
= []

>> " foo(baz(), 1)  doop() ":find_all("[..id](?)")
= ["foo(baz(), 1)", "doop()"]

>> "":find_all("")
= []

>> "Hello":find_all("")
= []
```

---

## `has`

**Description:**  
Checks if the `Text` contains a target pattern (see: [Patterns](#Patterns)).

**Usage:**  
```tomo
has(text: Text, pattern: Text) -> Bool
```

**Parameters:**

- `text`: The text to be searched.
- `pattern`: The pattern to search for.

**Returns:**  
`yes` if the target pattern is found, `no` otherwise.

**Example:**  
```tomo
>> "hello world":has("wo")
= yes
>> "hello world":has("[..alpha]")
= yes
>> "hello world":has("[..digit]")
= no
>> "hello world":has("[..start]he")
= yes
```

---

## `join`

**Description:**  
Joins an array of text pieces with a specified glue.

**Usage:**  
```tomo
join(glue: Text, pieces: [Text]) -> Text
```

**Parameters:**

- `glue`: The text used to join the pieces.
- `pieces`: The array of text pieces to be joined.

**Returns:**  
A single `Text` value with the pieces joined by the glue.

**Example:**  
```tomo
>> ", ":join(["one", "two", "three"])
= "one, two, three"
```

---

## `lines`

**Description:**  
Splits the text into an array of lines of text, preserving blank lines,
ignoring trailing newlines, and handling `\r\n` the same as `\n`.

**Usage:**  
```tomo
split(text: Text) -> [Text]
```

**Parameters:**

- `text`: The text to be split into lines.

**Returns:**  
An array of substrings resulting from the split.

**Example:**  
```tomo
>> "one$(\n)two$(\n)three":lines()
= ["one", "two", "three"]
>> "one$(\n)two$(\n)three$(\n)":lines()
= ["one", "two", "three"]
>> "one$(\n)two$(\n)three$(\n\n)":lines()
= ["one", "two", "three", ""]
>> "one$(\r\n)two$(\r\n)three$(\r\n)":lines()
= ["one", "two", "three"]
>> "":lines()
= []
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
>> "AMÉLIE":lower()
= "amélie"
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
>> "one$(\n)two":quoted()
= "\"one\\ntwo\""
```

---

## `replace`

**Description:**  
Replaces occurrences of a pattern in the text with a replacement string.
See [Patterns](#patterns) for more information about patterns.

**Usage:**  
```tomo
replace(text: Text, pattern: Text, replacement: Text) -> Text
```

**Parameters:**

- `text`: The text in which to perform replacements.
- `pattern`: The pattern to be replaced.
- `replacement`: The text to replace the pattern with.

**Returns:**  
The text with occurrences of the pattern replaced.

**Example:**  
```tomo
>> "Hello world":replace("world", "there")
= "Hello there"

>> "Hello world":replace("[..id]", "xxx")
= "xxx xxx"
```

---

## `split`

**Description:**  
Splits the text into an array of substrings based on a pattern.
See [Patterns](#patterns) for more information about patterns.

**Usage:**  
```tomo
split(text: Text, pattern: Text = "") -> [Text]
```

**Parameters:**

- `text`: The text to be split.
- `pattern`: The pattern used to split the text. If the pattern is the empty
  string, the text will be split into individual grapheme clusters.

**Returns:**  
An array of substrings resulting from the split.

**Example:**  
```tomo
>> "one,two,three":split(",")
= ["one", "two", "three"]

>> "abc":split()
= ["a", "b", "c"]

>> "a    b  c":split("[..space]")
= ["a", "b", "c"]

>> "a,b,c,":split(",")
= ["a", "b", "c", ""]
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
>> "amélie":title()
= "Amélie"
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
>> "amélie":upper()
= "AMÉLIE"
```
