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

- [`func as_c_string(text: Text -> CString)`](#`as_c_string)
- [`func at(text: Text, index: Int -> Text)`](#`at)
- [`func by_line(text: Text -> func(->Text?))`](#`by_line)
- [`func by_match(text: Text, pattern: Pattern -> func(->Match?))`](#`by_match)
- [`func by_split(text: Text, pattern: Pattern = $// -> func(->Text?))`](#`by_split)
- [`func bytes(text: Text -> [Byte])`](#`bytes)
- [`func codepoint_names(text: Text -> [Text])`](#`codepoint_names)
- [`func each(text: Text, pattern: Pattern, fn: func(m: Match), recursive: Bool = yes -> Int?)`](#`each)
- [`func ends_with(text: Text, suffix: Text -> Bool)`](#`ends_with)
- [`func find(text: Text, pattern: Pattern, start: Int = 1 -> Int?)`](#`find)
- [`func find_all(text: Text, pattern: Pattern -> [Match])`](#`find_all)
- [`func from(text: Text, first: Int -> Text)`](#`from)
- [`func from_codepoint_names(codepoints: [Int32] -> [Text])`](#`from_bytes)
- [`func from_c_string(str: CString -> Text)`](#`from_c_string)
- [`func from_codepoint_names(codepoint_names: [Text] -> [Text])`](#`from_codepoint_names)
- [`func from_codepoint_names(codepoints: [Int32] -> [Text])`](#`from_codepoints)
- [`func has(text: Text, pattern: Pattern -> Bool)`](#`has)
- [`func join(glue: Text, pieces: [Text] -> Text)`](#`join)
- [`func split(text: Text -> [Text])`](#`lines)
- [`func lower(text: Text -> Text)`](#`lower)
- [`func map(text: Text, pattern: Pattern, fn: func(text:Match)->Text -> Text, recursive: Bool = yes)`](#`map)
- [`func matches(text: Text, pattern: Pattern -> [Text])`](#`matches)
- [`func quoted(text: Text, color: Bool = no -> Text)`](#`quoted)
- [`func repeat(text: Text, count:Int -> Text)`](#`repeat)
- [`func replace(text: Text, pattern: Pattern, replacement: Text, backref: Pattern = $/\/, recursive: Bool = yes -> Text)`](#`replace)
- [`func replace_all(replacements:{Pattern,Text}, backref: Pattern = $/\/, recursive: Bool = yes -> Text)`](#`replace_all)
- [`func reversed(text: Text -> Text)`](#`reversed)
- [`func slice(text: Text, from: Int = 1, to: Int = -1 -> Text)`](#`slice)
- [`func split(text: Text, pattern: Pattern = "" -> [Text])`](#`split)
- [`func starts_with(text: Text, prefix: Text -> Bool)`](#`starts_with)
- [`func title(text: Text -> Text)`](#`title)
- [`func to(text: Text, last: Int -> Text)`](#`to)
- [`func trim(text: Text, pattern: Pattern = $/{whitespace/, trim_left: Bool = yes, trim_right: Bool = yes -> Text)`](#`trim)
- [`func upper(text: Text -> Text)`](#`upper)
- [`func utf32_codepoints(text: Text -> [Int32])`](#`utf32_codepoints)

### `as_c_string`

**Description:**  
Converts a `Text` value to a C-style string.

**Signature:**  
```tomo
func as_c_string(text: Text -> CString)
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

### `at`

**Description:**  
Get the graphical cluster at a given index. This is similar to `str[i]` with
ASCII text, but has more correct behavior for unicode text.

**Signature:**  
```tomo
func at(text: Text, index: Int -> Text)
```

**Parameters:**

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

**Description:**  
Returns an iterator function that can be used to iterate over the lines in a
text.

**Signature:**  
```tomo
func by_line(text: Text -> func(->Text?))
```

**Parameters:**

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

**Description:**  
Returns an iterator function that can be used to iterate over the occurrences
of a pattern in a text.

**Signature:**  
```tomo
func by_match(text: Text, pattern: Pattern -> func(->Match?))
```

**Parameters:**

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

**Description:**  
Returns an iterator function that can be used to iterate over text separated by
a pattern.

**Signature:**  
```tomo
func by_split(text: Text, pattern: Pattern = $// -> func(->Text?))
```

**Parameters:**

- `text`: The text to be iterated over in pattern-delimited chunks.
- `pattern`: The [pattern](patterns.md) to split the text on.

**Returns:**  
An iterator function that returns one chunk of text at a time, separated by the
given pattern, until it runs out and returns `none`. **Note:** using an empty
pattern (the default) will iterate over single grapheme clusters in the text.

**Example:**  
```tomo
text := "one,two,three"
for chunk in text:by_split($/,/):
    # Prints: "one" then "two" then "three":
    say(chunk)
```

---

### `bytes`

**Description:**  
Converts a `Text` value to an array of bytes representing a UTF8 encoding of
the text.

**Signature:**  
```tomo
func bytes(text: Text -> [Byte])
```

**Parameters:**

- `text`: The text to be converted to UTF8 bytes.

**Returns:**  
An array of bytes (`[Byte]`) representing the text in UTF8 encoding.

**Example:**  
```tomo
>> "Amélie":bytes()
= [65[B], 109[B], 195[B], 169[B], 108[B], 105[B], 101[B]] : [Byte]
```

---

### `codepoint_names`

**Description:**  
Returns an array of the names of each codepoint in the text.

**Signature:**  
```tomo
func codepoint_names(text: Text -> [Text])
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

### `each`

**Description:**  
Iterates over each match of a [pattern](patterns.md) and passes the match to
the given function.

**Signature:**  
```tomo
func each(text: Text, pattern: Pattern, fn: func(m: Match), recursive: Bool = yes -> Int?)
```

**Parameters:**

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

**Description:**  
Checks if the `Text` ends with a literal suffix text.

**Signature:**  
```tomo
func ends_with(text: Text, suffix: Text -> Bool)
```

**Parameters:**

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

**Description:**  
Finds the first occurrence of a [pattern](patterns.md) in the given text (if
any).

**Signature:**  
```tomo
func find(text: Text, pattern: Pattern, start: Int = 1 -> Int?)
```

**Parameters:**

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

**Description:**  
Finds all occurrences of a [pattern](patterns.md) in the given text.

**Signature:**  
```tomo
func find_all(text: Text, pattern: Pattern -> [Match])
```

**Parameters:**

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

**Description:**  
Get a slice of the text, starting at the given position.

**Signature:**  
```tomo
func from(text: Text, first: Int -> Text)
```

**Parameters:**

- `text`: The text to be sliced.
- `frist`: The index of the first grapheme cluster to include (1-indexed).

**Returns:**  
The text from the given grapheme cluster to the end of the text. Note: a
negative index counts backwards from the end of the text, so `-1` refers to the
last cluster, `-2` the second-to-last, etc. Slice ranges will be truncated to
the length of the string.

**Example:**  
```tomo
>> "hello":from(2)
= "ello"

>> "hello":from(-2)
= "lo"
```

---

### `from_bytes`

**Description:**  
Returns text that has been constructed from the given UTF8 bytes. Note: the
text will be normalized, so the resulting text's UTF8 bytes may not exactly
match the input.

**Signature:**  
```tomo
func from_codepoint_names(codepoints: [Int32] -> [Text])
```

**Parameters:**

- `codepoints`: The UTF32 codepoints in the desired text.

**Returns:**  
A new text based on the input UTF8 bytes after normalization has been applied.

**Example:**  
```tomo
>> Text.from_bytes([195[B], 133[B], 107[B], 101[B]])
= "Åke"
```

---

### `from_c_string`

**Description:**  
Converts a C-style string to a `Text` value.

**Signature:**  
```tomo
func from_c_string(str: CString -> Text)
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

### `from_codepoint_names`

**Description:**  
Returns text that has the given codepoint names (according to the Unicode
specification) as its codepoints. Note: the text will be normalized, so the
resulting text's codepoints may not exactly match the input codepoints.

**Signature:**  
```tomo
func from_codepoint_names(codepoint_names: [Text] -> [Text])
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

### `from_codepoints`

**Description:**  
Returns text that has been constructed from the given UTF32 codepoints. Note:
the text will be normalized, so the resulting text's codepoints may not exactly
match the input codepoints.

**Signature:**  
```tomo
func from_codepoint_names(codepoints: [Int32] -> [Text])
```

**Parameters:**

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

**Description:**  
Checks if the `Text` contains a target [pattern](patterns.md).

**Signature:**  
```tomo
func has(text: Text, pattern: Pattern -> Bool)
```

**Parameters:**

- `text`: The text to be searched.
- `pattern`: The [pattern](patterns.md) to search for.

**Returns:**  
`yes` if the target pattern is found, `no` otherwise.

**Example:**  
```tomo
>> "hello world":has($/wo/)
= yes
>> "hello world":has($/{alpha}/)
= yes
>> "hello world":has($/{digit}/)
= no
>> "hello world":has($/{start}he/)
= yes
```

---

### `join`

**Description:**  
Joins an array of text pieces with a specified glue.

**Signature:**  
```tomo
func join(glue: Text, pieces: [Text] -> Text)
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

### `lines`

**Description:**  
Splits the text into an array of lines of text, preserving blank lines,
ignoring trailing newlines, and handling `\r\n` the same as `\n`.

**Signature:**  
```tomo
func split(text: Text -> [Text])
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

### `lower`

**Description:**  
Converts all characters in the text to lowercase.

**Signature:**  
```tomo
func lower(text: Text -> Text)
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

### `map`

**Description:**  
For each occurrence of the given [pattern](patterns.md), replace the text with
the result of calling the given function on that match.

**Signature:**  
```tomo
func map(text: Text, pattern: Pattern, fn: func(text:Match)->Text -> Text, recursive: Bool = yes)
```

**Parameters:**

- `text`: The text to be searched.
- `pattern`: The [pattern](patterns.md) to search for.
- `fn`: The function to apply to each match.
- `recursive`: Whether to recursively map `fn` to each of the captures of the
  pattern before handing them to `fn`.

**Returns:**  
The text with the matching parts replaced with the result of applying the given
function to each.

**Example:**  
```tomo
>> "hello world":map($/world/, func(m:Match): m.text:upper())
= "hello WORLD"
>> "Some nums: 1 2 3 4":map($/{int}/, func(m:Match): "$(Int.parse(m.text)! + 10)")
= "Some nums: 11 12 13 14"
```

---

### `matches`

**Description:**  
Checks if the `Text` matches target [pattern](patterns.md) and returns an array
of the matching text captures or a null value if the entire text doesn't match
the pattern.

**Signature:**  
```tomo
func matches(text: Text, pattern: Pattern -> [Text])
```

**Parameters:**

- `text`: The text to be searched.
- `pattern`: The [pattern](patterns.md) to search for.

**Returns:**  
An array of the matching text captures if the entire text matches the pattern,
or a null value otherwise.

**Example:**  
```tomo
>> "hello world":matches($/{id}/)
= none : [Text]?

>> "hello world":matches($/{id} {id}/)
= ["hello", "world"] : [Text]?
```

---

### `quoted`

**Description:**  
Formats the text as a quoted string.

**Signature:**  
```tomo
func quoted(text: Text, color: Bool = no -> Text)
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

### `repeat`

**Description:**  
Repeat some text multiple times.

**Signature:**  
```tomo
func repeat(text: Text, count:Int -> Text)
```

**Parameters:**

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

**Description:**  
Replaces occurrences of a [pattern](patterns.md) in the text with a replacement
string.

**Signature:**  
```tomo
func replace(text: Text, pattern: Pattern, replacement: Text, backref: Pattern = $/\/, recursive: Bool = yes -> Text)
```

**Parameters:**

- `text`: The text in which to perform replacements.
- `pattern`: The [pattern](patterns.md) to be replaced.
- `replacement`: The text to replace the pattern with.
- `backref`: If non-empty, the replacement text will have occurrences of this
  pattern followed by a number replaced with the corresponding backreference.
  By default, the backreference pattern is a single backslash, so
  backreferences look like `\0`, `\1`, etc.
- `recursive`: For backreferences of a nested capture, if recursive is set to
  `yes`, then the whole replacement will be reapplied recursively to the
  backreferenced text if it's used in the replacement.

**Backreferences**
If a backreference pattern is in the replacement, then that backreference is
replaced with the corresponding group from the matching text. Backreference
`0` is the entire matching text, backreference `1` is the first matched group,
and so on. Literal text is not captured for backreferences, only named group
captures (`{foo}`), quoted captures (`"?"`), and nested group captures (`(?)`).
For quoted and nested group captures, the backreference refers to the *inside*
of the capture without the enclosing punctuation.

If you need to insert a digit immediately after a backreference, you can use an
optional semicolon: `\1;2` (backref 1, followed by the replacement text`"2"`).

**Returns:**  
The text with occurrences of the pattern replaced.

**Example:**  
```tomo
>> "Hello world":replace($/world/, "there")
= "Hello there"

>> "Hello world":replace($/{id}/, "xxx")
= "xxx xxx"

>> "Hello world":replace($/{id}/, "\0")
= "(Hello) (world)"

>> "Hello world":replace($/{id}/, "(@0)", backref=$/@/)
= "(Hello) (world)"

>> "Hello world":replace($/{id} {id}/, "just \2")
= "just world"

# Recursive is the default behavior:
>> " BAD(x, BAD(y), z) ":replace($/BAD(?)/, "good(\1)", recursive=yes)
= " good(x, good(y), z) "

>> " BAD(x, BAD(y), z) ":replace($/BAD(?)/, "good(\1)", recursive=no)
= " good(x, BAD(y), z) "
```

---

### `replace_all`

**Description:**  
Takes a table mapping [patterns](patterns.md) to replacement texts and performs
all the replacements in the table on the whole text. At each position, the
first matching pattern's replacement is applied and the pattern matching moves
on to *after* the replacement text, so replacement text is not recursively
modified. See [`replace()`](#replace) for more information about replacement
behavior.

**Signature:**  
```tomo
func replace_all(replacements:{Pattern,Text}, backref: Pattern = $/\/, recursive: Bool = yes -> Text)
```

**Parameters:**

- `text`: The text in which to perform replacements.
- `replacements`: A table mapping from [pattern](patterns.md) to the
  replacement text associated with that pattern.
- `backref`: If non-empty, the replacement text will have occurrences of this
  pattern followed by a number replaced with the corresponding backreference.
  By default, the backreference pattern is a single backslash, so
  backreferences look like `\0`, `\1`, etc.
- `recursive`: For backreferences of a nested capture, if recursive is set to
  `yes`, then the matching replacement will be reapplied recursively to the
  backreferenced text if it's used in the replacement.

**Returns:**  
The text with all occurrences of the patterns replaced with their corresponding
replacement text.

**Example:**  
```tomo
>> "A <tag> & an amperand":replace_all({
    $/&/ = "&amp;",
    $/</ = "&lt;",
    $/>/ = "&gt;",
    $/"/ = "&quot",
    $/'/ = "&#39;",
}
= "A &lt;tag&gt; &amp; an ampersand"

>> "Hello":replace_all({$/{lower}/="[\0]", $/{upper}/="{\0}"})
= "{H}[ello]"
```

---

### `reversed`

**Description:**  
Return a text that has the grapheme clusters in reverse order.

**Signature:**  
```tomo
func reversed(text: Text -> Text)
```

**Parameters:**

- `text`: The text to reverse.

**Returns:**  
A reversed version of the text.

**Example:**  
```tomo
>> "Abc":reversed()
= "cbA"
```

---

### `slice`

**Description:**  
Get a slice of the text.

**Signature:**  
```tomo
func slice(text: Text, from: Int = 1, to: Int = -1 -> Text)
```

**Parameters:**

- `text`: The text to be sliced.
- `from`: The index of the first grapheme cluster to include (1-indexed).
- `to`: The index of the last grapheme cluster to include (1-indexed).

**Returns:**  
The text that spans the given grapheme cluster indices. Note: a negative index
counts backwards from the end of the text, so `-1` refers to the last cluster,
`-2` the second-to-last, etc. Slice ranges will be truncated to the length of
the string.

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

**Description:**  
Splits the text into an array of substrings based on a [pattern](patterns.md).

**Signature:**  
```tomo
func split(text: Text, pattern: Pattern = "" -> [Text])
```

**Parameters:**

- `text`: The text to be split.
- `pattern`: The [pattern](patterns.md) used to split the text. If the pattern
  is the empty string, the text will be split into individual grapheme clusters.

**Returns:**  
An array of substrings resulting from the split.

**Example:**  
```tomo
>> "one,two,three":split($/,/)
= ["one", "two", "three"]

>> "abc":split()
= ["a", "b", "c"]

>> "a    b  c":split($/{space}/)
= ["a", "b", "c"]

>> "a,b,c,":split($/,/)
= ["a", "b", "c", ""]
```

---

### `starts_with`

**Description:**  
Checks if the `Text` starts with a literal prefix text.

**Signature:**  
```tomo
func starts_with(text: Text, prefix: Text -> Bool)
```

**Parameters:**

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

**Description:**  
Converts the text to title case (capitalizing the first letter of each word).

**Signature:**  
```tomo
func title(text: Text -> Text)
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

### `to`

**Description:**  
Get a slice of the text, ending at the given position.

**Signature:**  
```tomo
func to(text: Text, last: Int -> Text)
```

**Parameters:**

- `text`: The text to be sliced.
- `last`: The index of the last grapheme cluster to include (1-indexed).

**Returns:**  
The text up to and including the given grapheme cluster. Note: a negative index
counts backwards from the end of the text, so `-1` refers to the last cluster,
`-2` the second-to-last, etc. Slice ranges will be truncated to the length of
the string.

**Example:**  
```tomo
>> "goodbye":to(3)
= "goo"

>> "goodbye":to(-2)
= "goodby"
```

---

### `trim`

**Description:**  
Trims the matching [pattern](patterns.md) from the left and/or right side of the text.

**Signature:**  
```tomo
func trim(text: Text, pattern: Pattern = $/{whitespace/, trim_left: Bool = yes, trim_right: Bool = yes -> Text)
```

**Parameters:**

- `text`: The text to be trimmed.
- `pattern`: The [pattern](patterns.md) that will be trimmed away.
- `trim_left`: Whether or not to trim from the front of the text.
- `trim_right`: Whether or not to trim from the back of the text.

**Returns:**  
The text without the trim pattern at either end.

**Example:**  
```tomo
>> "   x y z    $(\n)":trim()
= "x y z"

>> "abc123def":trim($/{!digit}/)
= "123"

>> "   xyz   ":trim(trim_right=no)
= "xyz   "
```

---

### `upper`

**Description:**  
Converts all characters in the text to uppercase.

**Signature:**  
```tomo
func upper(text: Text -> Text)
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

---

### `utf32_codepoints`

**Description:**  
Returns an array of Unicode code points for UTF32 encoding of the text.

**Signature:**  
```tomo
func utf32_codepoints(text: Text -> [Int32])
```

**Parameters:**

- `text`: The text from which to extract Unicode code points.

**Returns:**  
An array of 32-bit integer Unicode code points (`[Int32]`).

**Example:**  
```tomo
>> "Amélie":utf32_codepoints()
= [65[32], 109[32], 233[32], 108[32], 105[32], 101[32]] : [Int32]
```
