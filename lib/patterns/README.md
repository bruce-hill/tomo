# Text Pattern Matching

As an alternative to full regular expressions, Tomo provides a limited text
matching pattern syntax that is intended to solve 80% of use cases in under 1%
of the code size (PCRE's codebase is roughly 150k lines of code, and Tomo's
pattern matching code is a bit under 1k lines of code). Tomo's pattern matching
syntax is highly readable and works well for matching literal text without
getting [leaning toothpick syndrome](https://en.wikipedia.org/wiki/Leaning_toothpick_syndrome).

For more advanced use cases, consider linking against a C library for regular
expressions or pattern matching.

`Pat` is a [domain-specific language](docs/langs.md), in other words, it's
like a `Text`, but it has a distinct type.

Patterns are used in a small, but very powerful API that handles many text
functions that would normally be handled by a more extensive API:

- [`by_pattern(text:Text, pattern:Pat -> func(->PatternMatch?))`](#by_pattern)
- [`by_pattern_split(text:Text, pattern:Pat -> func(->Text?))`](#by_pattern_split)
- [`each_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch), recursive=yes)`](#each_pattern)
- [`find_patterns(text:Text, pattern:Pat -> [PatternMatch])`](#find_patterns)
- [`has_pattern(text:Text, pattern:Pat -> Bool)`](#has_pattern)
- [`map_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch -> Text), recursive=yes -> Text)`](#map_pattern)
- [`matches_pattern(text:Text, pattern:Pat -> Bool)`](#matches_pattern)
- [`pattern_captures(text:Text, pattern:Pat -> [Text]?)`](#pattern_captures)
- [`replace_pattern(text:Text, pattern:Pat, replacement:Text, backref="@", recursive=yes -> Text)`](#replace_pattern)
- [`split_pattern(text:Text, pattern:Pat -> [Text])`](#split_pattern)
- [`translate_patterns(text:Text, replacements:{Pat,Text}, backref="@", recursive=yes -> Text)`](#translate_patterns)
- [`trim_pattern(text:Text, pattern=$Pat"{space}", left=yes, right=yes -> Text)`](#trim_pattern)

## Matches

Pattern matching functions work with a type called `PatternMatch` that has three fields:

- `text`: The full text of the match.
- `index`: The index in the text where the match was found.
- `captures`: A list containing the matching text of each non-literal pattern group.

See [Text Functions](text.md#Text-Functions) for the full API documentation.

## Syntax

Patterns have three types of syntax:

- `{` followed by an optional count (`n`, `n-m`, or `n+`), followed by an
  optional `!` to negate the pattern, followed by an optional pattern name or
  Unicode character name, followed by a required `}`.

- Any matching pair of quotes or parentheses or braces with a `?` in the middle
  (e.g. `"?"` or `(?)`).

- Any other character is treated as a literal to be matched exactly.

## Named Patterns

Named patterns match certain pre-defined patterns that are commonly useful. To
use a named pattern, use the syntax `{name}`. Names are case-insensitive and
mostly ignore spaces, underscores, and dashes.

- `..` - Any character (note that a single `.` would mean the literal period
  character).
- `digit` - A unicode digit
- `email` - an email address
- `emoji` - an emoji
- `end` - the very end of the text
- `id` - A unicode identifier
- `int` - One or more digits with an optional `-` (minus sign) in front
- `ip` - an IP address (IPv4 or IPv6)
- `ipv4` - an IPv4 address
- `ipv6` - an IPv6 address
- `nl`/`newline`/`crlf` - A line break (either `\r\n` or `\n`)
- `num` - One or more digits with an optional `-` (minus sign) in front and an optional `.` and more digits after
- `start` - the very start of the text
- `uri` - a URI
- `url` - a URL (URI that specifically starts with `http://`, `https://`, `ws://`, `wss://`, or `ftp://`)
- `word` - A unicode identifier (same as `id`)

For non-alphabetic characters, any single character is treated as matching
exactly that character. For example, `{1{}` matches exactly one `{`
character. Or, `{1.}` matches exactly one `.` character.

Patterns can also use any Unicode property name. Some helpful ones are:

- `hex` - Hexidecimal digits
- `lower` - Lowercase letters
- `space` - The space character
- `upper` - Uppercase letters
- `whitespace` - Whitespace characters

Patterns may also use exact Unicode codepoint names. For example: `{1 latin
small letter A}` matches `a`.

## Negating Patterns

If an exclamation mark (`!`) is placed before a pattern's name, then characters
are matched only when they _don't_ match the pattern. For example, `{!alpha}`
will match all characters _except_ alphabetic ones.

## Interpolating Text and Escaping

To escape a character in a pattern (e.g. if you want to match the literal
character `?`), you can use the syntax `{1 ?}`. This is almost never necessary
unless you have text that looks like a Tomo text pattern and has something like
`{` or `(?)` inside it.

However, if you're trying to do an exact match of arbitrary text values, you'll
want to have the text automatically escaped. Fortunately, Tomo's injection-safe
DSL text interpolation supports automatic text escaping. This means that if you
use text interpolation with the `$` sign to insert a text value, the value will
be automatically escaped using the `{1 ?}` rule described above:

```tomo
# Risk of code injection (would cause an error because 'xxx' is not a valid
# pattern name:
>> user_input := get_user_input()
= "{xxx}"

# Interpolation automatically escapes:
>> $/$user_input/
= $/{1{}..xxx}/

# This is: `{ 1{ }` (one open brace) followed by the literal text "..xxx}"

# No error:
>> some_text.find($/$user_input/)
= 0
```

If you prefer, you can also use this to insert literal characters:

```tomo
>> $/literal $"{..}"/
= $/literal {1{}..}/
```

## Repetitions

By default, named patterns match 1 or more repetitions, but you can specify how
many repetitions you want by putting a number or range of numbers first using
`n` (exactly `n` repetitions), `n-m` (between `n` and `m` repetitions), or `n+`
(`n` or more repetitions):

```
{4-5 alpha}
0x{hex}
{4 digit}-{2 digit}-{2 digit}
{2+ space}
{0-1 question mark}
```


# Methods

### `by_pattern`
Returns an iterator function that yields `PatternMatch` objects for each occurrence.

```tomo
func by_pattern(text:Text, pattern:Pat -> func(->PatternMatch?))
```

- `text`: The text to search.
- `pattern`: The pattern to match.

**Returns:**
An iterator function that yields `PatternMatch` objects one at a time.

**Example:**
```tomo
text := "one, two, three"
for word in text.by_pattern($Pat"{id}"):
    say(word.text)
```

---

### `by_pattern_split`
Returns an iterator function that yields text segments split by a pattern.

```tomo
func by_pattern_split(text:Text, pattern:Pat -> func(->Text?))
```

- `text`: The text to split.
- `pattern`: The pattern to use as a separator.

**Returns:**
An iterator function that yields text segments.

**Example:**
```tomo
text := "one two three"
for word in text.by_pattern_split($Pat"{whitespace}"):
    say(word.text)
```

---

### `each_pattern`
Applies a function to each occurrence of a pattern in the text.

```tomo
func each_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch), recursive=yes)
```

- `text`: The text to search.
- `pattern`: The pattern to match.
- `fn`: The function to apply to each match.
- `recursive`: If `yes`, applies the function recursively on modified text.

**Example:**
```tomo
text := "one two three"
text.each_pattern($Pat"{id}", func(m:PatternMatch):
    say(m.txt)
)
```

---

### `find_patterns`
Finds all occurrences of a pattern in a text and returns them as `PatternMatch` objects.

```tomo
func find_patterns(text:Text, pattern:Pat -> [PatternMatch])
```

- `text`: The text to search.
- `pattern`: The pattern to match.

**Returns:**
A list of `PatternMatch` objects.

**Example:**
```tomo
text := "one! two three!"
>> text.find_patterns($Pat"{id}!")
= [PatternMatch(text="one!", index=1, captures=["one"]), PatternMatch(text="three!", index=10, captures=["three"])]
```

---

### `has_pattern`
Checks whether a given pattern appears in the text.

```tomo
func has_pattern(text:Text, pattern:Pat -> Bool)
```

- `text`: The text to search.
- `pattern`: The pattern to check for.

**Returns:**
`yes` if a match is found, otherwise `no`.

**Example:**
```tomo
text := "...okay..."
>> text.has_pattern($Pat"{id}")
= yes
```

---

### `map_pattern`
Transforms matches of a pattern using a mapping function.

```tomo
func map_pattern(text:Text, pattern:Pat, fn:func(m:PatternMatch -> Text), recursive=yes -> Text)
```

- `text`: The text to modify.
- `pattern`: The pattern to match.
- `fn`: A function that transforms matches.
- `recursive`: If `yes`, applies transformations recursively.

**Returns:**
A new text with the transformed matches.

**Example:**
```tomo
text := "I have #apples and #oranges and #plums"
fruits := {"apples"=4, "oranges"=5}
>> text.map_pattern($Pat'#{id}', func(match:PatternMatch):
    fruit := match.captures[1]
    "$(fruits[fruit] or 0) $fruit"
)
= "I have 4 apples and 5 oranges and 0 plums"
```

---

### `matches_pattern`
Returns whether or not text matches a pattern completely.

```tomo
func matches_pattern(text:Text, pattern:Pat -> Bool)
```

- `text`: The text to match against.
- `pattern`: The pattern to match.

**Returns:**
`yes` if the whole text matches the pattern, otherwise `no`.

**Example:**
```tomo
>> "Hello!!!".matches_pattern($Pat"{id}")
= no
>> "Hello".matches_pattern($Pat"{id}")
= yes
```

---

### `pattern_captures`
Returns a list of pattern captures for the given pattern.

```tomo
func pattern_captures(text:Text, pattern:Pat -> [Text]?)
```

- `text`: The text to match against.
- `pattern`: The pattern to match.

**Returns:**
An optional list of matched pattern captures. Returns `none` if the text does
not match the pattern.

**Example:**
```tomo
>> "123 boxes".pattern_captures($Pat"{int} {id}")
= ["123", "boxes"]?
>> "xxx".pattern_captures($Pat"{int} {id}")
= none
```

---

### `replace_pattern`
Replaces occurrences of a pattern with a replacement text, supporting backreferences.

```tomo
func replace_pattern(text:Text, pattern:Pat, replacement:Text, backref="@", recursive=yes -> Text)
```

- `text`: The text to modify.
- `pattern`: The pattern to match.
- `replacement`: The text to replace matches with.
- `backref`: The symbol for backreferences in the replacement.
- `recursive`: If `yes`, applies replacements recursively.

**Returns:**
A new text with replacements applied.

**Example:**
```tomo
>> "I have 123 apples and 456 oranges".replace_pattern($Pat"{int}", "some")
= "I have some apples and some oranges"

>> "I have 123 apples and 456 oranges".replace_pattern($Pat"{int}", "(@1)")
= "I have (123) apples and (456) oranges"

>> "I have 123 apples and 456 oranges".replace_pattern($Pat"{int}", "(?1)", backref="?")
= "I have (123) apples and (456) oranges"

>> "bad(fn(), bad(notbad))".replace_pattern($Pat"bad(?)", "good(@1)")
= "good(fn(), good(notbad))"

>> "bad(fn(), bad(notbad))".replace_pattern($Pat"bad(?)", "good(@1)", recursive=no)
= "good(fn(), bad(notbad))"
```

---

### `split_pattern`
Splits a text into segments using a pattern as the delimiter.

```tomo
func split_pattern(text:Text, pattern:Pat -> [Text])
```

- `text`: The text to split.
- `pattern`: The pattern to use as a separator.

**Returns:**
A list of text segments.

**Example:**
```tomo
>> "one two three".split_pattern($Pat"{whitespace}")
= ["one", "two", "three"]
```

---

### `translate_patterns`
Replaces multiple patterns using a mapping of patterns to replacement texts.

```tomo
func translate_patterns(text:Text, replacements:{Pat,Text}, backref="@", recursive=yes -> Text)
```

- `text`: The text to modify.
- `replacements`: A table mapping patterns to their replacements.
- `backref`: The symbol for backreferences in replacements.
- `recursive`: If `yes`, applies replacements recursively.

**Returns:**
A new text with all specified replacements applied.

**Example:**
```tomo
>> text := "foo(x, baz(1))"
>> text.translate_patterns({
    $Pat"{id}(?)"="call(fn('@1'), @2)",
    $Pat"{id}"="var('@1')",
    $Pat"{int}"="int(@1)",
})
= "call(fn('foo'), var('x'), call(fn('baz'), int(1)))"
```

---

### `trim_pattern`
Removes matching patterns from the beginning and/or end of a text.

```tomo
func trim_pattern(text:Text, pattern=$Pat"{space}", left=yes, right=yes -> Text)
```

- `text`: The text to trim.
- `pattern`: The pattern to trim (defaults to whitespace).
- `left`: If `yes`, trims from the beginning.
- `right`: If `yes`, trims from the end.

**Returns:**
The trimmed text.

**Example:**
```tomo
>> "123abc456".trim_pattern($Pat"{digit}")
= "abc"
```
