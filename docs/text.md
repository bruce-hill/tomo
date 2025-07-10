# Text

`Text` is Tomo's datatype to represent text. The name `Text` is used instead of
"string" because Tomo text represents immutable, normalized unicode data with
fast indexing that has a tree-based implementation that is efficient for
concatenation. These are _not_ C-style NUL-terminated character arrays. GNU
libunistring is used for full Unicode functionality (grapheme cluster counts,
capitalization, etc.).

## Implementation

Internally, Tomo text's implementation is based on [Raku/MoarVM's
strings](https://docs.raku.org/language/unicode) and [Boehm et al's
Cords/Ropes](https://www.cs.tufts.edu/comp/150FP/archive/hans-boehm/ropes.pdf).
Texts store their grapheme cluster count and either a compact list of 8-bit
ASCII characters (for ASCII text), a list of 32-bit normal-form grapheme
cluster values (see below), a compressed form of grapheme clusters with a
lookup table, or a (roughly) balanced binary tree representing a concatenation.
The upside of this approach is that repeated concatenations are typically a
constant-time operation, which will occasionally require a small rebalancing
operation. Text is stored in a format that is highly memory-efficient and
index-based text operations (like retrieving an arbitrary index or slicing) are
very fast: typically a constant-time operation for arbitrary unicode text, but
in the worst case scenario (text built from many concatenations), `O(log(n))`
time with very generous constant factors typically amounting to only a handful
of steps. Since concatenations use shared substructures, they are very
memory-efficient and can be used efficiently for applications like implementing
a text editor that stores a full edit history of a large file's contents.

### Normal-Form Graphemes

In order to maintain compact storage, fast indexing, and fast slicing,
non-ASCII text is stored as 32-bit normal-form graphemes. A normal-form
grapheme is either a positive value representing a Unicode codepoint that
corresponds to a grapheme cluster (most Unicode letters used in natural
language fall into this category after normalization) or a negative value
representing an index into an internal list of "synthetic grapheme cluster
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
- `👩🏽‍🚀` is a single graphical cluster, but it's made up of several
  combining codepoints (`["WOMAN", "EMOJI MODIFIER FITZPATRICK TYPE-4", "ZERO
  WITDH JOINER", "ROCKET"]`). Since this can't be represented with a single
  codepoint, we must create a synthetic codepoint for it. If this was the 3rd
  synthetic codepoint that we've found, then we would represent it with the
  number `-3`, which can be used to look it up in a lookup table. The lookup
  table holds the full sequence of codepoints used in the grapheme cluster.

### Text Compression

One common property in natural language text is that most text is comprised of a
relatively small set of grapheme clusters that are frequently reused. To take
advantage of this property, Tomo's Text implementation scans along in new text
objects looking for spans of text that use 256 or fewer unique grapheme clusters
with many repetitions. If such a span is found, the text is stored using a small
translation table and one 8-bit unsigned integer per grapheme cluster in that
chunk (which is possible when there are 256 or fewer clusters in the text). This
means that, for the cost of a small array of 32-bit integers, we can store the
entire text using only one byte per grapheme cluster instead of a full 32-bit
integer. For example, a Greek-language text will typically use the Greek
alphabet, plus a few punctuation marks. Thus, if you have a text with a few
thousand Greek letters, we can efficiently store the text as a small lookup
table of around a hundred 32-bit integers and use one byte per "letter" in the
text. This representation is around 4x more efficient than using the UTF32
representation to store each Unicode codepoint as a 32-bit integer, and it's
about 2x more efficient than using UTF8 to store each non-ASCII codepoint as a
multi-byte sequence. Different languages will have different efficiencies, but
in general, text will be stored significantly more efficiently than UTF32 and
somewhat more efficiently than UTF8. However, the big advantage of this approach
is that we get the ability to do constant-time random access of grapheme
clusters, while getting space efficiency that is almost always better than
variable-width UTF8 encoding (which does not support fast random access of any
kind).

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

Like other languages, backslash is a special character inside of text for
escape sequences like `\n`. However, in general it is best practice to use
multi-line text if you need to add a newline.

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
"string builder" class in the language and no need to use a list of text
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
its constituent grapheme clusters with `text.split()` and iterate over those.

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

# API

[API documentation](../api/text.md)
