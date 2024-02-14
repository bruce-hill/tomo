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

Inside a double quoted string, you can use curly braces (`{...}`) to insert an
expression that you want converted to a string. This is called string
interpolation:

```
// Interpolation:
str := "Sum: {1 + 2}"
// equivalent to "Sum: 3"
```

Single-quoted strings do not have interpolations:

```
// No interpolation here:
str := 'Sum: {1 + 2}'
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

These string literals can be used as interpolation values:

```
two_lines := "one{\n}two"
has_quotes := "some {\"}quotes{\"} here"
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
nested := $$(I can have (parens) inside (parens inside (parens)))
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

### Advanced $-Strings

Sometimes you need to use many `{`s or `"`s inside a string, but you don't want
to type `{\{}` or `{\"}` each time. In such cases, you can use the more
advanced form of strings. The advanced form lets you explicitly specify which
characters are used for interpolation and which characters are used for
opening/closing the string. Advanced strings begin with a dollar sign (`$`),
followed by what interpolation style to use, followed by the character to use
to delimit the string, followed by the string contents and a closing string
delimiter. The interpolation style can be a matching pair (`()`, `[]`, `{}`, or
`<>`) or any other single character. When the interpolation style is a matching
pair, the interpolation is any expression enclosed in that pair (e.g.
`${}"interpolate {1 + 2}"`). When the interpolation style is a single
character, the interpolation must be either a parenthesized expression or a
single term with no infix operators (e.g. a variable), for example:
`$@"Interpolate @var or @(1 + 2)"`.

Here are some examples:

```
$[]"In here, quotes delimit the string and square brackets interpolate: [1 + 2]"
$@"For single-letter interpolations, the interpolation is a single term like @my_var without a closing symbol"
$@"But you can parenthesize expressions like: @(x + y) if you need to"
$$"Double dollars means dollar signs interpolate: $my_var $(1 + 2)"
$${If you have a string with "quotes" and 'single quotes', you can choose something else like curly braces to delimit the string}
$?#Here hashes delimit the string and question marks interpolate: ?(1 + 2)#
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

- Number of grapheme clusters: `string.num_graphemes()`
- Size in bytes: `string.num_bytes()`
- Number of unicode codepoints: `string.num_codepoints()` (you probably want to
  use graphemes, not codepoints in most applications)

### Iteration

Iteration is *not* supported for strings because of the ambiguity between
bytes, codepoints, and graphemes. It is instead recommended that you use
higher-abstraction functions.

### Subcomponents

- `string.bytes()` returns an array of `Int8` bytes
- `string.codepoints()` returns an array of `Int32` bytes
- `string.graphemes()` returns an array of grapheme cluster strings
- `string.words()` returns an array of word strings
- `string.lines()` returns an array of line strings
- `string.split(",", empty=no)` returns an array of strings split by the given delimiter

### Equality and Comparison

By default, strings are compared using memory comparisons of the UTF-8 representation.

- `x == y` is roughly equivalent to `strcmp(x, y) == 0`

To compare normalized forms of strings, use:

- `x.equivalent_to(y)` returns a boolean for whether the strings are the same
- `x.compare_normalized(y)` returns `enum(Equal, Less, Greater)`

### Capitalization

- `x.capitalized()`
- `x.titlecased()`
- `x.uppercased()`
- `x.lowercased()`

### Patterns

- `string.has($/pattern/, at=Anywhere:enum(Anywhere, Start, End))` Check whether a pattern can be found
- `string.next($/pattern/)` Returns an `enum(NotFound, Found(match:Str, rest:Str))`
- `string.matches($/pattern/)` Returns a list of matching strings
- `string.replace($/pattern/, "replacement")` Returns a copy of the string with replacements
- `string.without($/pattern/, at=Anywhere:enum(Anywhere, Start, End))`

### Indentation

- `string.indented(type:enum(Tab, Spaces(num:Int), count=1)` (e.g. `s.indented(Tab)`, `s.indented(Spaces(4), -1)`

### Properties

Unicode strings have various overlapping properties. For example, a grapheme
might be both printable and alphabetic. It can be useful to query some of these
properties for a given string.

- `string.properties() -> flags(None, WhiteSpace, Alphabetic, …, Emoji, …)`
- `string.is(properties:flags(None, WhiteSpace, Alphabetic, …, Emoji, …)) -> Bool`
- `string.has_property(properties:flags(None, WhiteSpace, Alphabetic, …, Emoji, …)) -> Bool`

Example: `if name.is(Uppercase)`
Example: `if name.is(Alphabetic or Numeric)`
Example: `if name.has_property(Math or Currency)`
