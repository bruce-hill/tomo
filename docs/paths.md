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

# API

[API documentation](../api/paths.md)
