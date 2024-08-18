# Strings

Strings are implemented as immutable UTF-8-encoded values using:

- The Boehm Cord library for efficient storage and concatenation.
- GNU libunistring for unicode functionality (grapheme cluster counts,
  capitalization, etc.)
- My own BP library for simple pattern matching operations (similar to regex)

## Syntax

Strings have a flexible syntax designed to make it easy to hold values from
different languages without the need to have lots of escape sequences and
without using printf-style string formatting.

```
// Basic string:
str := "Hello world"
str2 := 'Also a string'
str3 := `Backticks too`
```

## Line Splits

Long strings can be split across multiple lines by having two or more dots at
the start of a new line on the same indentation level that started the string:

```
str := "This is a long
.... line that is split in code"
```

## Multi-line Strings

Multi-line strings have indented (i.e. at least one tab more than the start of
the string) text inside quotation marks. The leading and trailing newline are
ignored:

```
multi_line := "
    This string has multiple lines.
    Line two.

    You can split a line
.... using two or more dots to make an elipsis.

    Remember to include whitespace after the elipsis if desired.

    Or don't if you're splitting a long word like supercalifragilisticexpia
....lidocious

        This text is indented by one level in the string

    "quotes" are ignored unless they're at the same indentation level as the
.... start of the string.

    The end (no newline after this).
"
```

## String Interpolations

Inside a double quoted string, you can use a dollar sign (`$`) to insert an
expression that you want converted to a string. This is called string
interpolation:

```
// Interpolation:
my_var := 5
str := "My var is $my_var!"
// Equivalent to "My var is 5!"

// Using parentheses:
str := "Sum: $(1 + 2)"
// equivalent to "Sum: 3"
```

Single-quoted strings do not have interpolations:

```
// No interpolation here:
str := 'Sum: $(1 + 2)'
```

## String Escapes

Unlike other languages, backslash is *not* a special character inside of a
string. For example, `"x\ny"` has the characters `x`, `\`, `n`, `y`, not a
newline. Instead, a series of character escapes act as complete string literals
without quotation marks:

```
newline := \n
crlf := \r\n
quote := \"
```

These string literals can be used as interpolation values with or without
parentheses, depending on which you find more readable:

```
two_lines := "one$(\n)two"
has_quotes := "some $\"quotes$\" here"
```

However, in general it is best practice to use multi-line strings to avoid these problems:

```
str := "
    This has
    multiple lines and "quotes" too!
"
```

### Multi-line Strings

There are two reasons for strings to span multiple lines in code: either you
have a string that contains newlines and you want to represent it without `\n`
escapes, or you have a long single-line string that you want to split across
multiple lines for readability. To support this, you can use newlines inside of
strings with indentation-sensitivity. For splitting long lines, use two or more
"."s at the same indentation level as the start of the string literal:

```
single_line := "This is a long string that
.... spans multiple lines"
```
For strings that contain newlines, you may put multiple indented lines inside
the quotes:

```
multi_line := "
    line one
    line two
        this line is indented
    last line
"
```

Strings may only end on lines with the same indentation as the starting quote
and nested quotes are ignored:

```
multi_line := "
    Quotes in indented regions like this: " don't count
"
```

If there is a leading or trailing newline, it is ignored and not included in
the string.

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

### Customizable $-Strings

Sometimes you might need to use a lot of literal `$`s or quotation marks in a
string. In such cases, you can use the more customizable form of strings. The
customizable form lets you explicitly specify which character to use for
interpolation and which characters to use for delimiting the string.

The first character after the `$` is the custom interpolation character, which
can be any of the following symbols: `~!@#$%^&*+=\?`. If none of these
characters is used, the default interpolation character is `$`. Since this is
the default, you can disable interpolation altogether by using `$` here (i.e. a
double `$$`).

The next thing in a customizable string is the character used to delimit the
string. The string delimiter can be any of the following symbols: `` "'`|/;([{< ``
If the string delimiter is one of `([{<`, then the string will continue until a
matching `)]}>` is found, not terminating unless the delimiters are balanced
(i.e. nested pairs of delimiters are considered part of the string).

Here are some examples:

```
$"Equivalent to a normal string with dollar interps: $(1 + 2)"
$@"The same, but the AT symbol interpolates: @(1 + 2)"
$$"No interpolation here, $ is just a literal character"
$|This string is pipe-delimited, so it can have "quotes" and 'single quotes' and interpolates with dollar sign: $(1+2)|
$(This string is parens-delimited, so you can have (nested) parens without ending the string)
$=[This string is square-bracket delimited [which can be nested] and uses equals for interps: =(1 + 2)]
$@/look ma, regex literals!/
```

When strings are delimited by matching pairs (`()`, `[]`, `{}`, or `<>`), they
can only be closed by a matched closing character at the same indentation
level, ignoring nested pairs:

```
$$(Inside parens, you can have (nested ()) parens no problem)
$$"But only (), [], {}, and <> are matching pairs, you can't have nested quotes"
$$(
    When indented, an unmatched ) won't close the string
    An unmatched ( won't mess things up either
    Only matching pairs on the same indentation level are counted:
)
$$(Multi-line string with nested (parens) and
.. line continuation)
```

As a special case, when you use the same character for interpolation and string
delimiting, no interpolations are allowed:

```
plain := $""This string has {no interpolations}!"
```

**Note:** Normal doubly quoted strings with no dollar sign (e.g. `"foo"`) are a
shorthand for `${}"foo"`. Singly quoted strings with no dollar sign (e.g.
`'foo'`) are shorthand for `$''foo'`.

## Operations

### Concatenation

Concatenation in the typical case is an O(1) operation: `"{x}{y}"` or `x ++ y`.

Because string concatenation is typically an O(1) operation, there is no need
for a separate string builder class in the language and no need to use an array
of string fragments.

### String Length

String length is an ambiguous term in the context of UTF-8 strings. There are
several possible meanings, so each of these meanings is split into a separate
method:

- Number of grapheme clusters: `string:num_graphemes()`
- Size in bytes: `string:num_bytes()`
- Number of unicode codepoints: `string:num_codepoints()` (you probably want to
  use graphemes, not codepoints in most applications)

Since the typical user expectation is that string length refers to "letters,"
the `#` length operator returns the number of grapheme clusters, which is the
closest unicode equivalent to "letters."

### Iteration

Iteration is *not* supported for strings because of the ambiguity between
bytes, codepoints, and graphemes. It is instead recommended that you explicitly
iterate over bytes, codepoints, graphemes, words, lines, etc:

### Subcomponents

- `string:bytes()` returns an array of `Int8` bytes
- `string:codepoints()` returns an array of `Int32` bytes
- `string:graphemes()` returns an array of grapheme cluster strings
- `string:words()` returns an array of word strings
- `string:lines()` returns an array of line strings
- `string:split(",", empty=no)` returns an array of strings split by the given delimiter

### Equality, Comparison, and Hashing

All text is compared and hashed using unicode normalization. Unicode provides
several different ways to represent the same text. For example, the single
codepoint `U+E9` (latin small e with accent) is rendered the same as the two
code points `U+65 U+301` (latin small e, acute combining accent) and has an
equivalent linguistic meaning. These are simply different ways to represent the
same "letter." In order to make it easy to write correct code that takes this
into account, Tomo uses unicode normalization for all string comparisons and
hashing. Normalization does the equivalent of converting text to a canonical
form before performing comparisons or hashing. This means that if a table is
created that has text with the codepoint `U+E9` as a key, then a lookup with
the same text but with `U+65 U+301` instead of `U+E9` will still succeed in
finding the value because the two strings are equivalent under normalization.

### Capitalization

- `x:capitalized()`
- `x:titlecased()`
- `x:uppercased()`
- `x:lowercased()`

### Patterns

- `string:has("target", at=Where.Anywhere|Where.Start|Where.End)->Bool` Check whether a pattern can be found
- `string:without("target", at=Where.Anywhere|Where.Start|Where.End)->Text`
- `string:trimmed("chars...", at=Where.Anywhere|Where.Start|Where.End)->Text`
- `string:find("target")->enum(Failure, Success(index:Int32))`
- `string:replace("target", "replacement", limit=Int.max)->Text` Returns a copy of the string with replacements
- `string:split("split")->[Text]`
- `string:join(["one", "two"])->Text`
