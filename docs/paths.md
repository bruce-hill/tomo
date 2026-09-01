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

Path literals are normalized: `.` and `..` components are resolved, repeated
and trailing slashes are dropped, and the result is what the program carries.
A literal contains no interpolation, so all of this happens while compiling
rather than on every run:

```tomo
assert (/foo/../bar) == (/bar)
assert (./path/to/) == (./path/to)
```

`.`, `..`, `/`, and `~` mean themselves and are left alone, and nothing
resolves a `..` against a `~`, which stands in for a directory whose location
is not known until the program runs. Paths built while running -- with
`Path.from_text`, `.child()`, or `++` -- are normalized then, since their text
is not available any earlier.

## Usage

Paths come with a bunch of methods for doing filesystem operations. Broadly,
they fall into three categories:

- File reading and writing, e.g. `(./foo.txt).write("Hello world")!`
- Filesystem traversal and metadata, e.g. `(/some/dir).children()!`
- Path manipulation, e.g. `(./file.jpg).extension()`

Operations that involve the filesystem will typically return a `Result` type,
which is either `Success` or `Failure{reason}`. In the common case where you
expect a file operation to succeed and do not want to explicitly account for
the possibility of a file operation failing, you can use `!` as a suffix as a
shorthand for "if this returns `Failure{reason}`, then fail and print the
reason."

## Path Form is Preserved

Methods that hand back paths derived from a path you gave them --
`children()`, `files()`, `subdirectories()`, `each_child()`, `walk()`, `glob()`,
`child()`, `parent()`, `unique_directory()`, and `write_unique()` -- return
paths in the same form as the one they started from. A relative path stays
relative and a `~` path stays home-based:

```tomo
assert (./foo).children() == [(./foo/a.txt)]
assert (~/foo).children() == [(~/foo/a.txt)]
assert (/foo).children() == [(/foo/a.txt)]
```

This means a path can be printed back to the user, or written to a file, in
the terms they wrote it in. When you do want an absolute path, ask for one with
`.resolved()`.

## Concurrency

Tomo programs are not the only thing that can touch the filesystem, so path
operations avoid the shape where a program asks a question and then acts on the
answer -- between the two, another process can make the answer wrong. Instead
they attempt the operation and read what the system says about it:

- `Path.move` with `overwrite=no` asks the kernel to rename only if the
  destination does not exist, as one indivisible operation.
- `Path.remove` attempts the removal and learns from the error what was there,
  rather than looking first and then deleting what it saw.
- `Path.create_directory` treats an already-existing directory as success,
  which is what makes it safe to call when something else may be creating the
  same directory.

Two operations cannot be made indivisible and say so in their documentation:
`Path.copy_to` is many operations over a tree rather than one, and moving a
directory across filesystems is a copy in disguise.

`Path.exists`, `Path.can_read`, `Path.can_write`, and `Path.can_execute` answer
about the moment they are asked. They are useful for reporting, but deciding
what to do from one is the race this section is about -- attempt the operation
and handle its `Failure` instead.

## Internal Representation

Paths are internally represented as C-style NUL-terminated char strings. This
makes it easy to use them with C APIs without any conversion. Paths do **not**
perform any unicode normalization, unlike `Text`.

A consequence is that a `Path` can hold bytes that are not valid UTF-8, because
POSIX filenames are byte strings and a real file on disk may be named with any
bytes at all. `Text`, by contrast, holds Unicode. Converting between them can
therefore fail, so `Text(path)` returns a `Text?` rather than a `Text`:

```tomo
if name := Text(path)
    say("The path is $name")
else
    say("This path is not valid UTF-8")

# Or assume it converts and error if it doesn't:
name := Text(path)!
```

The same is true of `Text(cstring)` and `CString.as_text`, for the same reason.

`Path.base_name`, `Path.extension`, and `Path.components` produce `Text` out of
the same bytes, so they are optional too. `Path.has_extension` is not: it
compares raw bytes and never decodes, so it keeps working on names that
`Path.extension` cannot represent.

# API

[API documentation](../api/paths.md)
