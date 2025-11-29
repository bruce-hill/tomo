# Paths and Files

Tomo supports a built-in syntax for file and directory paths, with some logic
to help prevent or mitigate the risks of errors caused by string manipulations
of file paths. Tomo does not have a built-in datatype to represent files
specifically, but instead relies on Paths as the API to do filesystem
operations.

## Syntax

Paths are [domain-specific languages](langs.md) that have their own dedicated
syntax. A path literal begins with either `(/`, `(./`, `(../`, or `(~/` and continues
until a matching closing parenethesis:

```tomo
assert (/tmp) == (/tmp)
assert (~/path with/(parens) is/ok/) == (~/path with/(parens) is/ok/)
```

### Interpolation

Paths can contain interpolations using `$`, just like strings. However, there are
certain values that _cannot_ be interpolated:

- The literal string `.`
- The literal string `..`
- Any text that contains a forward slash (`/`)

The intended use for path interpolation is to take user input which may or may
not be trustworthy and interpret that value as a single path component name,
i.e. the name of a directory or file. If a user were to supply a value like
`..` or `foo/baz`, it would risk navigating into a directory other than
intended. Paths can be created from text with slashes using
`Path.from_text(text)` if you need to use arbitrary text as a file path.

# API

[API documentation](../api/paths.md)
