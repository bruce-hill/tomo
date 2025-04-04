# Text

`Text` is Tomo's datatype to represent text. The name `Text` is used instead of
"string" because Tomo text represents immutable, normalized unicode data with
fast indexing that has an implementation that is efficient for concatenation.
These are _not_ C-style NUL-terminated character arrays. GNU libunistring is
used for full Unicode functionality (grapheme cluster counts, capitalization,
etc.).

## Implementation

Internally, Tomo text's implementation is based on [Raku/MoarVM's
strings](https://docs.raku.org/language/unicode) and [Boehm et al's
Cords](https://www.cs.tufts.edu/comp/150FP/archive/hans-boehm/ropes.pdf).
Strings store their grapheme cluster count and either a compact array of 8-bit
ASCII characters (for ASCII text), an array of 32-bit normal-form grapheme
cluster values (see below), or a (roughly) balanced binary tree concatenation
of two texts. The upside is that repeated concatenations are typically a
constant-time operation, which will occasionally require a small rebalancing
operation. Index-based text operations (like retrieving an arbitrary index or
slicing) are very fast: typically a constant-time operation for arbitrary
unicode text, but in the worst case scenario (text built from many
concatenations), `O(log(n))` time with very generous constant factors typically
amounting to only a handful of steps. Since concatenations use shared
substructures, they are very memory-efficient and can be used efficiently for
applications like implementing a text editor that stores a full edit history of
a large file's contents.

### Normal-Form Graphemes

In order to maintain compact storage, fast indexing, and fast slicing,
non-ASCII text is stored as 32-bit normal-form graphemes. A normal-form
grapheme is either a positive value representing a Unicode codepoint that
corresponds to a grapheme cluster (most Unicode letters used in natural
language fall into this category after normalization) or a negative value
representing an index into an internal array of "synthetic grapheme cluster
codepoints." Here are some examples:

- `A` is a normal codepoint that is also a grapheme cluster, so it would
  be represented as the number `65`
- `ABC` is three separate grapheme clusters, one for `A`, one for `B`, one
  for `C`.
- `Å` is also a single codepoint (`LATIN CAPITAL LETTER A WITH RING ABOVE`)
  that is also a grapheme cluster, so it would be represented as the number
  `197`.
- `家` (Japanese for "house") is a single codepoint (`CJK Unified
  Ideograph-5BB6`) that is also a grapheme cluster, so it would be represented
  as the number `23478`
-`👩🏽‍🚀` is a single graphical cluster, but it's made up of several
  combining codepoints (`["WOMAN", "EMOJI MODIFIER FITZPATRICK TYPE-4", "ZERO
  WITDH JOINER", "ROCKET"]`). Since this can't be represented with a single
  codepoint, we must create a synthetic codepoint for it. If this was the `n`th
  synthetic codepoint that we've found, then we would represent it with the
  number `-n`, which can be used to look it up in a lookup table. The lookup
  table holds the full sequence of codepoints used in the grapheme cluster.

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

### Line Splits

Long text can be split across multiple lines by having two or more dots at the
start of a new line on the same indentation level that started the text:

```
str := "This is a long
....... line that is split in code"
```

### Multi-line Text

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

### Text Escapes

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

Concatenation in the typical case is a fast operation: `"{x}{y}"` or `x ++ y`.

Because text concatenation is typically fast, there is no need for a separate
"string builder" class in the language and no need to use an array of text
fragments.

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

## Patterns

Texts use a custom pattern matching syntax for text matching and replacement as
a lightweight, but powerful alternative to regular expressions. See [the
pattern documentation](patterns.md) for more details.

## Text Functions

- [`func as_c_string(text: Text -> CString)`](#as_c_string)
- [`func at(text: Text, index: Int -> Text)`](#at)
- [`func by_line(text: Text -> func(->Text?))`](#by_line)
- [`func by_split(text: Text, delimiter: Text = "" -> func(->Text?))`](#by_split)
- [`func by_split_any(text: Text, delimiters: Text = " $\t\r\n" -> func(->Text?))`](#by_split_any)
- [`func bytes(text: Text -> [Byte])`](#bytes)
- [`func caseless_equals(a: Text, b:Text, language:Text = "C" -> Bool)`](#caseless_equals)
- [`func codepoint_names(text: Text -> [Text])`](#codepoint_names)
- [`func ends_with(text: Text, suffix: Text -> Bool)`](#ends_with)
- [`func from(text: Text, first: Int -> Text)`](#from)
- [`func from_bytes(codepoints: [Int32] -> [Text])`](#from_bytes)
- [`func from_c_string(str: CString -> Text)`](#from_c_string)
- [`func from_codepoint_names(codepoint_names: [Text] -> [Text])`](#from_codepoint_names)
- [`func from_codepoints(codepoints: [Int32] -> [Text])`](#from_codepoints)
- [`func has(text: Text, target: Text -> Bool)`](#has)
- [`func join(glue: Text, pieces: [Text] -> Text)`](#join)
- [`func split(text: Text, delimiter: Text = "" -> [Text])`](#split)
- [`func split_any(text: Text, delimiters: Text = " $\t\r\n" -> [Text])`](#split_any)
- [`func middle_pad(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)`](#middle_pad)
- [`func left_pad(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)`](#left_pad)
- [`func lines(text: Text -> [Text])`](#lines)
- [`func lower(text: Text, language: Text = "C" -> Text)`](#lower)
- [`func quoted(text: Text, color: Bool = no, quotation_mark: Text = '"' -> Text)`](#quoted)
- [`func repeat(text: Text, count:Int -> Text)`](#repeat)
- [`func replace(text: Text, target: Text, replacement: Text -> Text)`](#replace)
- [`func reversed(text: Text -> Text)`](#reversed)
- [`func right_pad(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)`](#right_pad)
- [`func slice(text: Text, from: Int = 1, to: Int = -1 -> Text)`](#slice)
- [`func starts_with(text: Text, prefix: Text -> Bool)`](#starts_with)
- [`func title(text: Text, language: Text = "C" -> Text)`](#title)
- [`func to(text: Text, last: Int -> Text)`](#to)
- [`func translate(translations:{Text,Text} -> Text)`](#translate)
- [`func trim(text: Text, to_trim: Text = " $\t\r\n", left: Bool = yes, right: Bool = yes -> Text)`](#trim)
- [`func upper(text: Text, language: Text "C" -> Text)`](#upper)
- [`func utf32_codepoints(text: Text -> [Int32])`](#utf32_codepoints)
- [`func width(text: Text -> Int)`](#width)
- [`func without_prefix(text: Text, prefix: Text -> Text)`](#without_prefix)
- [`func without_suffix(text: Text, suffix: Text -> Text)`](#without_suffix)

----------------

### `as_c_string`
Converts a `Text` value to a C-style string.

```tomo
func as_c_string(text: Text -> CString)
```

- `text`: The text to be converted to a C-style string.

**Returns:**  
A C-style string (`CString`) representing the text.

**Example:**  
```tomo
>> "Hello":as_c_string()
= CString("Hello")
```

---

### `at`
Get the graphical cluster at a given index. This is similar to `str[i]` with
ASCII text, but has more correct behavior for unicode text.

```tomo
func at(text: Text, index: Int -> Text)
```

- `text`: The text from which to get a cluster.
- `index`: The index of the graphical cluster (1-indexed).

**Returns:**  
A `Text` with the single graphical cluster at the given index. Note: negative
indices are counted from the back of the text, so `-1` means the last cluster,
`-2` means the second-to-last, and so on.

**Example:**  
```tomo
>> "Amélie":at(3)
= "é"
```

---

### `by_line`
Returns an iterator function that can be used to iterate over the lines in a
text.

```tomo
func by_line(text: Text -> func(->Text?))
```

- `text`: The text to be iterated over, line by line.

**Returns:**  
An iterator function that returns one line at a time, until it runs out and
returns `none`. **Note:** this function ignores a trailing newline if there is
one. If you don't want this behavior, use `text:by_split($/{1 nl}/)` instead.

**Example:**  
```tomo
text := "
    line one
    line two
"
for line in text:by_line():
    # Prints: "line one" then "line two":
    say(line)
```

---

### `by_match`
Returns an iterator function that can be used to iterate over the occurrences
of a pattern in a text.

```tomo
func by_match(text: Text, pattern: Pattern -> func(->Match?))
```

- `text`: The text to be iterated over looking for matches.
- `pattern`: The [pattern](patterns.md) to look for.

**Returns:**  
An iterator function that returns one match result at a time, until it runs out
and returns `none`. **Note:** if a zero-length match is found, the iterator
will return it exactly once. Matches are always non-overlapping.

**Example:**  
```tomo
text := "one two three"
for match in text:by_match($/{alpha}/):
    # Prints: "one" then "two" then "three":
    say(match.text)
```

---

### `by_split`
Returns an iterator function that can be used to iterate over text separated by
a delimiter.
**Note:** to split based on a set of delimiters, use [`by_split_any()`](#by_split_any).

```tomo
func by_split(text: Text, delimiter: Text = "" -> func(->Text?))
```

- `text`: The text to be iterated over in pattern-delimited chunks.
- `delimiter`: An exact delimiter to use for splitting the text. If an empty text
  is given, then each split will be the graphical clusters of the text.

**Returns:**  
An iterator function that returns one chunk of text at a time, separated by the
given delimiter, until it runs out and returns `none`. **Note:** using an empty
delimiter (the default) will iterate over single grapheme clusters in the text.

**Example:**  
```tomo
text := "one,two,three"
for chunk in text:by_split(","):
    # Prints: "one" then "two" then "three":
    say(chunk)
```

---

### `by_split_any`
Returns an iterator function that can be used to iterate over text separated by
one or more characters (grapheme clusters) from a given text of delimiters.
**Note:** to split based on an exact delimiter, use [`by_split()`](#by_split).

```tomo
func by_split_any(text: Text, delimiters: Text = " $\t\r\n" -> func(->Text?))
```

- `text`: The text to be iterated over in pattern-delimited chunks.
- `delimiters`: An text containing multiple delimiter characters (grapheme clusters)
  to use for splitting the text.

**Returns:**  
An iterator function that returns one chunk of text at a time, separated by the
given delimiter characters, until it runs out and returns `none`.

**Example:**  
```tomo
text := "one,two,;,three"
for chunk in text:by_split_any(",;"):
    # Prints: "one" then "two" then "three":
    say(chunk)
```

---

### `bytes`
Converts a `Text` value to an array of bytes representing a UTF8 encoding of
the text.

```tomo
func bytes(text: Text -> [Byte])
```

- `text`: The text to be converted to UTF8 bytes.

**Returns:**  
An array of bytes (`[Byte]`) representing the text in UTF8 encoding.

**Example:**  
```tomo
>> "Amélie":bytes()
= [65[B], 109[B], 195[B], 169[B], 108[B], 105[B], 101[B]] : [Byte]
```

---

### `caseless_equals`
Checks whether two texts are equal, ignoring the casing of the letters (i.e.
case-insensitive comparison).

```tomo
func caseless_equals(a: Text, b:Text, language:Text = "C" -> Bool)
```

- `a`: The first text to compare case-insensitively.
- `b`: The second text to compare case-insensitively.
- `language`: The ISO 639 language code for which casing rules to use.

**Returns:**  
`yes` if `a` and `b` are equal to each other, ignoring casing, otherwise `no`.

**Example:**  
```tomo
>> "A":caseless_equals("a")
= yes

# Turkish lowercase "I" is "ı" (dotless I), not "i"
>> "I":caseless_equals("i", language="tr_TR")
= no
```

---

### `codepoint_names`
Returns an array of the names of each codepoint in the text.

```tomo
func codepoint_names(text: Text -> [Text])
```

- `text`: The text from which to extract codepoint names.

**Returns:**  
An array of codepoint names (`[Text]`).

**Example:**  
```tomo
>> "Amélie":codepoint_names()
= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]
```

---

### `each`
Iterates over each match of a [pattern](patterns.md) and passes the match to
the given function.

```tomo
func each(text: Text, pattern: Pattern, fn: func(m: Match), recursive: Bool = yes -> Int?)
```

- `text`: The text to be searched.
- `pattern`: The [pattern](patterns.md) to search for.
- `fn`: A function to be called on each match that was found.
- `recursive`: For each match, if recursive is set to `yes`, then call `each()`
  recursively on its captures before calling `fn` on the match.

**Returns:**  
None.

**Example:**  
```tomo
>> " #one   #two  #three   ":each($/#{word}/, func(m:Match):
    say("Found word $(m.captures[1])")
)
```

---

### `ends_with`
Checks if the `Text` ends with a literal suffix text.

```tomo
func ends_with(text: Text, suffix: Text -> Bool)
```

- `text`: The text to be searched.
- `suffix`: The literal suffix text to check for.

**Returns:**  
`yes` if the text has the target, `no` otherwise.

**Example:**  
```tomo
>> "hello world":ends_with("world")
= yes
```

---

### `find`
Finds the first occurrence of a [pattern](patterns.md) in the given text (if
any).

```tomo
func find(text: Text, pattern: Pattern, start: Int = 1 -> Int?)
```

- `text`: The text to be searched.
- `pattern`: The [pattern](patterns.md) to search for.
- `start`: The index to start the search.

**Returns:**  
`!Match` if the target [pattern](patterns.md) is not found, otherwise a `Match`
struct containing information about the match.

**Example:**  
```tomo
>> " #one   #two  #three   ":find($/#{id}/, start=-999)
= none : Match?
>> " #one   #two  #three   ":find($/#{id}/, start=999)
= none : Match?
>> " #one   #two  #three   ":find($/#{id}/)
= Match(text="#one", index=2, captures=["one"]) : Match?
>> " #one   #two  #three   ":find("{id}", start=6)
= Match(text="#two", index=9, captures=["two"]) : Match?
```

---

### `find_all`
Finds all occurrences of a [pattern](patterns.md) in the given text.

```tomo
func find_all(text: Text, pattern: Pattern -> [Match])
```

- `text`: The text to be searched.
- `pattern`: The [pattern](patterns.md) to search for.

**Returns:**  
An array of every match of the [pattern](patterns.md) in the given text.
Note: if `text` or `pattern` is empty, an empty array will be returned.

**Example:**  
```tomo
>> " #one  #two #three   ":find_all($/#{alpha}/)
= [Match(text="#one", index=2, captures=["one"]), Match(text="#two", index=8, captures=["two"]), Match(text="#three", index=13, captures=["three"])]

>> "    ":find_all("{alpha}")
= []

>> " foo(baz(), 1)  doop() ":find_all("{id}(?)")
= [Match(text="foo(baz(), 1)", index=2, captures=["foo", "baz(), 1"]), Match(text="doop()", index=17, captures=["doop", ""])]

>> "":find_all($//)
= []

>> "Hello":find_all($//)
= []
```

---

### `from`
Get a slice of the text, starting at the given position.

```tomo
func from(text: Text, first: Int -> Text)
```

- `text`: The text to be sliced.
- `frist`: The index of the first grapheme cluster to include (1-indexed).

**Returns:**  
The text from the given grapheme cluster to the end of the text. Note: a
negative index counts backwards from the end of the text, so `-1` refers to the
last cluster, `-2` the second-to-last, etc. Slice ranges will be truncated to
the length of the text.

**Example:**  
```tomo
>> "hello":from(2)
= "ello"

>> "hello":from(-2)
= "lo"
```

---

### `from_bytes`
Returns text that has been constructed from the given UTF8 bytes. Note: the
text will be normalized, so the resulting text's UTF8 bytes may not exactly
match the input.

```tomo
func from_bytes(bytes: [Byte] -> [Text])
```

- `bytes`: The UTF-8 bytes of the desired text.

**Returns:**  
A new text based on the input UTF8 bytes after normalization has been applied.

**Example:**  
```tomo
>> Text.from_bytes([195[B], 133[B], 107[B], 101[B]])
= "Åke"
```

---

### `from_c_string`
Converts a C-style string to a `Text` value.

```tomo
func from_c_string(str: CString -> Text)
```

- `str`: The C-style string to be converted.

**Returns:**  
A `Text` value representing the C-style string.

**Example:**  
```tomo
>> Text.from_c_string(CString("Hello"))
= "Hello"
```

---

### `from_codepoint_names`
Returns text that has the given codepoint names (according to the Unicode
specification) as its codepoints. Note: the text will be normalized, so the
resulting text's codepoints may not exactly match the input codepoints.

```tomo
func from_codepoint_names(codepoint_names: [Text] -> [Text])
```

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

### `from_codepoints`
Returns text that has been constructed from the given UTF32 codepoints. Note:
the text will be normalized, so the resulting text's codepoints may not exactly
match the input codepoints.

```tomo
func from_codepoints(codepoints: [Int32] -> [Text])
```

- `codepoints`: The UTF32 codepoints in the desired text.

**Returns:**  
A new text with the specified codepoints after normalization has been applied.

**Example:**  
```tomo
>> Text.from_codepoints([197[32], 107[32], 101[32]])
= "Åke"
```

---

### `has`
Checks if the `Text` contains some target text.

```tomo
func has(text: Text, target: Text -> Bool)
```

- `text`: The text to be searched.
- `target`: The text to search for.

**Returns:**  
`yes` if the target text is found, `no` otherwise.

**Example:**  
```tomo
>> "hello world":has("wo")
= yes
>> "hello world":has("xxx")
= no
```

---

### `join`
Joins an array of text pieces with a specified glue.

```tomo
func join(glue: Text, pieces: [Text] -> Text)
```

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

### `middle_pad`
Pad some text on the left and right side so it reaches a target width.

```tomo
func middle_pad(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)
```

- `text`: The text to pad.
- `width`: The target width.
- `pad`: The padding text (default: `" "`).
- `language`: The ISO 639 language code for which character width to use.

**Returns:**  
Text with length at least `width`, with extra padding on the left and right as
needed. If `pad` has length greater than 1, it may be partially repeated to
reach the exact desired length.

**Example:**  
```tomo
>> "x":middle_pad(6)
= "  x   "
>> "x":middle_pad(10, "ABC")
= "ABCAxABCAB"
```

---

### `left_pad`
Pad some text on the left side so it reaches a target width.

```tomo
func left_pad(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)
```

- `text`: The text to pad.
- `width`: The target width.
- `pad`: The padding text (default: `" "`).
- `language`: The ISO 639 language code for which character width to use.

**Returns:**  
Text with length at least `width`, with extra padding on the left as needed. If
`pad` has length greater than 1, it may be partially repeated to reach the
exact desired length.

**Example:**  
```tomo
>> "x":left_pad(5)
= "    x"
>> "x":left_pad(5, "ABC")
= "ABCAx"
```

---

### `lines`
Splits the text into an array of lines of text, preserving blank lines,
ignoring trailing newlines, and handling `\r\n` the same as `\n`.

```tomo
func lines(text: Text -> [Text])
```

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

### `lower`
Converts all characters in the text to lowercase.

```tomo
func lower(text: Text, language: Text = "C" -> Text)
```

- `text`: The text to be converted to lowercase.
- `language`: The ISO 639 language code for which casing rules to use.

**Returns:**  
The lowercase version of the text.

**Example:**  
```tomo
>> "AMÉLIE":lower()
= "amélie"

>> "I":lower(language="tr_TR")
>> "ı"
```

---

### `quoted`
Formats the text with quotation marks and escapes.

```tomo
func quoted(text: Text, color: Bool = no, quotation_mark: Text = '"' -> Text)
```

- `text`: The text to be quoted.
- `color`: Whether to add color formatting (default is `no`).
- `quotation_mark`: The quotation mark to use (default is `"`).

**Returns:**  
The text formatted as a quoted text.

**Example:**  
```tomo
>> "one$(\n)two":quoted()
= "\"one\\ntwo\""
```

---

### `repeat`
Repeat some text multiple times.

```tomo
func repeat(text: Text, count:Int -> Text)
```

- `text`: The text to repeat.
- `count`: The number of times to repeat it. (Negative numbers are equivalent to zero).

**Returns:**  
The text repeated the given number of times.

**Example:**  
```tomo
>> "Abc":repeat(3)
= "AbcAbcAbc"
```

---

### `replace`
Replaces occurrences of a target text with a replacement text.

```tomo
func replace(text: Text, target: Text, replacement: Text -> Text)
```

- `text`: The text in which to perform replacements.
- `target`: The target text to be replaced.
- `replacement`: The text to replace the target with.

**Returns:**  
The text with occurrences of the target replaced.

**Example:**  
```tomo
>> "Hello world":replace("world", "there")
= "Hello there"
```

---

### `reversed`
Return a text that has the grapheme clusters in reverse order.

```tomo
func reversed(text: Text -> Text)
```

- `text`: The text to reverse.

**Returns:**  
A reversed version of the text.

**Example:**  
```tomo
>> "Abc":reversed()
= "cbA"
```

---

### `right_pad`
Pad some text on the right side so it reaches a target width.

```tomo
func right_pad(text: Text, width: Int, pad: Text = " ", language: Text = "C" -> Text)
```

- `text`: The text to pad.
- `width`: The target width.
- `pad`: The padding text (default: `" "`).
- `language`: The ISO 639 language code for which character width to use.

**Returns:**  
Text with length at least `width`, with extra padding on the right as needed. If
`pad` has length greater than 1, it may be partially repeated to reach the
exact desired length.

**Example:**  
```tomo
>> "x":right_pad(5)
= "x    "
>> "x":right_pad(5, "ABC")
= "xABCA"
```

---

### `slice`
Get a slice of the text.

```tomo
func slice(text: Text, from: Int = 1, to: Int = -1 -> Text)
```

- `text`: The text to be sliced.
- `from`: The index of the first grapheme cluster to include (1-indexed).
- `to`: The index of the last grapheme cluster to include (1-indexed).

**Returns:**  
The text that spans the given grapheme cluster indices. Note: a negative index
counts backwards from the end of the text, so `-1` refers to the last cluster,
`-2` the second-to-last, etc. Slice ranges will be truncated to the length of
the text.

**Example:**  
```tomo
>> "hello":slice(2, 3)
= "el"

>> "hello":slice(to=-2)
= "hell"

>> "hello":slice(from=2)
= "ello"
```

---

### `split`
Splits the text into an array of substrings based on exact matches of a delimiter.
**Note:** to split based on a set of delimiter characters, use [`split_any()`](#split_any).

```tomo
func split(text: Text, delimiter: Text = "" -> [Text])
```

- `text`: The text to be split.
- `delimiter`: The delimiter used to split the text. If the delimiter is the
  empty text, the text will be split into individual grapheme clusters.

**Returns:**  
An array of subtexts resulting from the split.

**Example:**  
```tomo
>> "one,two,,three":split(",")
= ["one", "two", "", "three"]

>> "abc":split()
= ["a", "b", "c"]
```

---

### `split_any`
Splits the text into an array of substrings at one or more occurrences of a set
of delimiter characters (grapheme clusters).
**Note:** to split based on an exact delimiter, use [`split()`](#split).

```tomo
func split_any(text: Text, delimiters: Text = " $\t\r\n" -> [Text])
```

- `text`: The text to be split.
- `delimiters`: A text containing multiple delimiters to be used for 
  splitting the text into chunks.

**Returns:**  
An array of subtexts resulting from the split.

**Example:**  
```tomo
>> "one, two,,three":split_any(", ")
= ["one", "two", "three"]
```

---

### `starts_with`
Checks if the `Text` starts with a literal prefix text.

```tomo
func starts_with(text: Text, prefix: Text -> Bool)
```

- `text`: The text to be searched.
- `prefix`: The literal prefix text to check for.

**Returns:**  
`yes` if the text has the given prefix, `no` otherwise.

**Example:**  
```tomo
>> "hello world":starts_with("hello")
= yes
```

---

### `title`
Converts the text to title case (capitalizing the first letter of each word).

```tomo
func title(text: Text, language: Text = "C" -> Text)
```

- `text`: The text to be converted to title case.
- `language`: The ISO 639 language code for which casing rules to use.

**Returns:**  
The text in title case.

**Example:**  
```tomo
>> "amélie":title()
= "Amélie"

# In Turkish, uppercase "i" is "İ"
>> "i":title(language="tr_TR")
= "İ"
```

---

### `to`
Get a slice of the text, ending at the given position.

```tomo
func to(text: Text, last: Int -> Text)
```

- `text`: The text to be sliced.
- `last`: The index of the last grapheme cluster to include (1-indexed).

**Returns:**  
The text up to and including the given grapheme cluster. Note: a negative index
counts backwards from the end of the text, so `-1` refers to the last cluster,
`-2` the second-to-last, etc. Slice ranges will be truncated to the length of
the text.

**Example:**  
```tomo
>> "goodbye":to(3)
= "goo"

>> "goodbye":to(-2)
= "goodby"
```

---

### `translate`
Takes a table mapping target texts to their replacements and performs all the
replacements in the table on the whole text. At each position, the first
matching replacement is applied and the matching moves on to *after* the
replacement text, so replacement text is not recursively modified. See
[`replace()`](#replace) for more information about replacement behavior.

```tomo
func translate(translations:{Pattern,Text} -> Text)
```

- `text`: The text in which to perform replacements.
- `translations`: A table mapping from target text to its replacement.

**Returns:**  
The text with all occurrences of the patterns replaced with their corresponding
replacement text.

**Example:**  
```tomo
>> "A <tag> & an amperand":translate({
    "&" = "&amp;",
    "<" = "&lt;",
    ">" = "&gt;",
    '"" = "&quot",
    "'" = "&#39;",
}
= "A &lt;tag&gt; &amp; an ampersand"
```

---

### `trim`
Trims the given characters (grapheme clusters) from the left and/or right side of the text.

```tomo
func trim(text: Text, to_trim: Text = " $\t\r\n", left: Bool = yes, right: Bool = yes -> Text)
```

- `text`: The text to be trimmed.
- `to_trim`: The characters to remove from the left/right of the text.
- `left`: Whether or not to trim from the front of the text.
- `right`: Whether or not to trim from the back of the text.

**Returns:**  
The text without the trim characters at either end.

**Example:**  
```tomo
>> "   x y z    $(\n)":trim()
= "x y z"

>> "one,":trim(",")
= "one"

>> "   xyz   ":trim(right=no)
= "xyz   "
```

---

### `upper`
Converts all characters in the text to uppercase.

```tomo
func upper(text: Text, language: Text = "C" -> Text)
```

- `text`: The text to be converted to uppercase.
- `language`: The ISO 639 language code for which casing rules to use.

**Returns:**  
The uppercase version of the text.

**Example:**  
```tomo
>> "amélie":upper()
= "AMÉLIE"

# In Turkish, uppercase "i" is "İ"
>> "i":upper(language="tr_TR")
= "İ"
```

---

### `utf32_codepoints`
Returns an array of Unicode code points for UTF32 encoding of the text.

```tomo
func utf32_codepoints(text: Text -> [Int32])
```

- `text`: The text from which to extract Unicode code points.

**Returns:**  
An array of 32-bit integer Unicode code points (`[Int32]`).

**Example:**  
```tomo
>> "Amélie":utf32_codepoints()
= [65[32], 109[32], 233[32], 108[32], 105[32], 101[32]] : [Int32]
```

---

### `width`
Returns the display width of the text as seen in a terminal with appropriate
font rendering. This is usually the same as the text's `.length`, but there are
some characters like emojis that render wider than 1 cell.

**Warning:** This will not always be exactly accurate when your terminal's font
rendering can't handle some unicode displaying correctly.

```tomo
func width(text: Text -> Int)
```

- `text`: The text whose length you want.

**Returns:**  
An integer representing the display width of the text.

**Example:**  
```tomo
>> "Amélie":width()
= 6
>> "🤠":width()
= 2
```

---

### `without_prefix`
Returns the text with a given prefix removed (if present).

```tomo
func without_prefix(text: Text, prefix: Text -> Text)
```

- `text`: The text to remove the prefix from.
- `prefix`: The prefix to remove.

**Returns:**  
A text without the given prefix (if present) or the unmodified text if the
prefix is not present.

**Example:**  
```tomo
>> "foo:baz":without_prefix("foo:")
= "baz"
>> "qux":without_prefix("foo:")
= "qux"
```

---

### `without_suffix`
Returns the text with a given suffix removed (if present).

```tomo
func without_suffix(text: Text, suffix: Text -> Text)
```

- `text`: The text to remove the suffix from.
- `suffix`: The suffix to remove.

**Returns:**  
A text without the given suffix (if present) or the unmodified text if the
suffix is not present.

**Example:**  
```tomo
>> "baz.foo":without_suffix(".foo")
= "baz"
>> "qux":without_suffix(".foo")
= "qux"
```
