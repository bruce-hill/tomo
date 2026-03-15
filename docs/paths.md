# Paths and Files

Tomo supports a built-in syntax for file and directory paths, with some logic
to help prevent or mitigate the risks of errors caused by string manipulations
of file paths. Tomo does not have a built-in datatype to represent files
specifically, but instead relies on Paths as the API to do filesystem
operations.

## Syntax

Paths are [domain-specific languages](langs.md) that have their own dedicated
syntax. A path literal begins with either `/`, `.`, or `~` and continues until
an unescaped space or the end of the line.

```tomo
path := /tmp/foo
path_with_spaces := ./file\ name.txt
```

Paths also respect parenthesis balancing, so a closing parenthesis is not
considered part of the path unless a corresponding opening parenthesis is also
part of the path, which makes it easier to use paths in many cases:

```tomo
paths := [(./foo), (./file(1).txt)]
contents := (./baz.txt).read()!
```

In the first example, the paths are parsed as `./foo` and `./file(1).txt`, not
`./foo),` and `./file(1).txt)]`. Similarly, in the second example, the path is
`./baz.txt`, not `./baz.txt).read()!`.

## Usage

Paths come with a bunch of methods for doing filesystem operations. Broadly,
they fall into three categories:

- File reading and writing, e.g. `(./foo.txt).write("Hello world")!`
- Filesystem traversal and metadata, e.g. `(/some/dir).children()!`
- Path manipulation, e.g. `(./file.jpg).extension()`

Operations that involve the filesystem will typically return a `Result` type,
which is either `Success` or `Failure(reason)`. In the common case where you
expect a file operation to succeed and do not want to explicitly account for
the possibility of a file operation failing, you can use `!` as a suffix as a
shorthand for "if this returns `Failure(reason)`, then fail and print the
reason."

## Internal Representation

Paths are internally represented as C-style NUL-terminated char strings. This
makes it easy to use them with C APIs without any conversion. Paths do **not**
perform any unicode normalization, unlike `Text`.

# API

[API documentation](../api/paths.md)
