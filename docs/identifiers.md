# Identifiers

Tomo identifiers (the names of variables, functions, arguments, struct/enum
fields, types, and so on) are fully Unicode-aware. An identifier may be written
using letters and symbols from any script, not just ASCII:

```tomo
func main()
    café := 42
    naïve := café + 1
    λ := 10
    数え := "counting"
    say("$naïve")
```

## What counts as a valid identifier

Tomo follows the standard defined by [Unicode Annex #31 (Unicode Identifier and
Pattern Syntax)](https://www.unicode.org/reports/tr31/):

- The first character must have the Unicode `XID_Start` property (this includes
  letters from every script), or be an underscore (`_`).
- Every subsequent character must have the Unicode `XID_Continue` property
  (letters, combining marks, digits, and a few connector characters like `_`).

This means digits and most punctuation cannot begin an identifier, and
whitespace, operators, and emoji are never part of one. Keywords (such as `func`,
`if`, `for`, ...) are still reserved and cannot be used as identifiers.

## Normalization

Unicode allows the same text to be encoded in more than one way. For example,
`é` can be written as a single precomposed code point (`U+00E9`) or as the
letter `e` followed by a combining acute accent (`U+0065 U+0301`). These look
identical but are different byte sequences.

To avoid confusing situations where two identifiers look the same but don't
refer to the same thing, Tomo normalizes every identifier to [Normalization
Form C (NFC)](https://www.unicode.org/reports/tr15/) at parse time. Both
unicode representations of `résumé` below refer to the same variable:

```tomo
func main()
    résumé := 7        # written with precomposed é
    say("$résumé")     # written with e + combining accent — same variable
```

## Generated C code

Identifiers are passed through to the generated C code (with a `_$` prefix to
avoid collisions), relying on the C compiler's support for extended (UTF-8)
identifiers. Tomo compiles with `zig cc` (Clang), which supports this, as do
recent versions of GCC and any C23-conforming compiler.
