% API

# Builtins

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
>> "Hello".as_c_string()
= CString("Hello")

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
>> "Amélie".at(3)
= "é"

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
>> "A".caseless_equals("a")
= yes

# Turkish lowercase "I" is "ı" (dotless I), not "i"
>> "I".caseless_equals("i", language="tr_TR")
= no

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
>> "Amélie".codepoint_names()
= ["LATIN CAPITAL LETTER A", "LATIN SMALL LETTER M", "LATIN SMALL LETTER E WITH ACUTE", "LATIN SMALL LETTER L", "LATIN SMALL LETTER I", "LATIN SMALL LETTER E"]

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
>> "hello world".ends_with("world")
= yes
remainder : Text
>> "hello world".ends_with("world", &remainder)
= yes
>> remainder
= "hello "

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
>> "hello".from(2)
= "ello"

>> "hello".from(-2)
= "lo"

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
>> Text.from_c_string(CString("Hello"))
= "Hello"

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
>> Text.from_codepoint_names([
"LATIN CAPITAL LETTER A WITH RING ABOVE",
"LATIN SMALL LETTER K",
"LATIN SMALL LETTER E",
]
= "Åke"

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
>> Text.from_utf16([197, 107, 101])
= "Åke"
>> Text.from_utf16([12371, 12435, 12395, 12385, 12399, 19990, 30028])
= "こんにちは世界".utf16()

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
>> Text.from_utf32([197, 107, 101])
= "Åke"

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
>> Text.from_utf8([195, 133, 107, 101])
= "Åke"

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
>> "hello world".has("wo")
= yes
>> "hello world".has("xxx")
= no

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
>> ", ".join(["one", "two", "three"])
= "one, two, three"

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
>> "x".left_pad(5)
= "    x"
>> "x".left_pad(5, "ABC")
= "ABCAx"

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
>> "one\ntwo\nthree".lines()
= ["one", "two", "three"]
>> "one\ntwo\nthree\n".lines()
= ["one", "two", "three"]
>> "one\ntwo\nthree\n\n".lines()
= ["one", "two", "three", ""]
>> "one\r\ntwo\r\nthree\r\n".lines()
= ["one", "two", "three"]
>> "".lines()
= []

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
>> "AMÉLIE".lower()
= "amélie"

>> "I".lower(language="tr_TR")
>> "ı"

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
>> "x".middle_pad(6)
= "  x   "
>> "x".middle_pad(10, "ABC")
= "ABCAxABCAB"

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
>> "one\ntwo".quoted()
= "\"one\\ntwo\""

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
>> "Abc".repeat(3)
= "AbcAbcAbc"

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
>> "Hello world".replace("world", "there")
= "Hello there"

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
>> "Abc".reversed()
= "cbA"

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
>> "x".right_pad(5)
= "x    "
>> "x".right_pad(5, "ABC")
= "xABCA"

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
>> "hello".slice(2, 3)
= "el"

>> "hello".slice(to=-2)
= "hell"

>> "hello".slice(from=2)
= "ello"

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
>> "one,two,,three".split(",")
= ["one", "two", "", "three"]

>> "abc".split()
= ["a", "b", "c"]

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
>> "one, two,,three".split_any(", ")
= ["one", "two", "three"]

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
>> "hello world".starts_with("hello")
= yes
remainder : Text
>> "hello world".starts_with("hello", &remainder)
= yes
>> remainder
= " world"

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
>> "amélie".title()
= "Amélie"

# In Turkish, uppercase "i" is "İ"
>> "i".title(language="tr_TR")
= "İ"

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
>> "goodbye".to(3)
= "goo"

>> "goodbye".to(-2)
= "goodby"

```
## Text.translate

```tomo
Text.translate : func(text: Text, translations: {Text=Text} -> Text)
```

Takes a table mapping target texts to their replacements and performs all the replacements in the table on the whole text. At each position, the first matching replacement is applied and the matching moves on to *after* the replacement text, so replacement text is not recursively modified. See Text.replace() for more information about replacement behavior.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to be translated.  | -
translations | `{Text=Text}` | A table mapping from target text to its replacement.  | -

**Return:** The text with all occurrences of the targets replaced with their corresponding replacement text.


**Example:**
```tomo
>> "A <tag> & an amperand".translate({
    "&" = "&amp;",
    "<" = "&lt;",
    ">" = "&gt;",
    '"" = "&quot",
    "'" = "&#39;",
})
= "A &lt;tag&gt; &amp; an ampersand"

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
>> "   x y z    \n".trim()
= "x y z"

>> "one,".trim(",")
= "one"

>> "   xyz   ".trim(right=no)
= "xyz   "

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
>> "amélie".upper()
= "AMÉLIE"

# In Turkish, uppercase "i" is "İ"
>> "i".upper(language="tr_TR")
= "İ"

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
>> "Åke".utf16()
= [197, 107, 101]
>> "こんにちは世界".utf16()
= [12371, 12435, 12395, 12385, 12399, 19990, 30028]

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
>> "Amélie".utf32()
= [65, 109, 233, 108, 105, 101]

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
>> "Amélie".utf8()
= [65, 109, 195, 169, 108, 105, 101]

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
>> "Amélie".width()
= 6
>> "🤠".width()
= 2

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
>> "foo:baz".without_prefix("foo:")
= "baz"
>> "qux".without_prefix("foo:")
= "qux"

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
>> "baz.foo".without_suffix(".foo")
= "baz"
>> "qux".without_suffix(".foo")
= "qux"

```
