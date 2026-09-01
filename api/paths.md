% API

# Builtins

# Path
## Path.accessed

```tomo
Path.accessed : func(path: Path, follow_symlinks: Bool = yes -> Int64?)
```

Gets the file access time of a file.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file whose access time you want.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** A 64-bit unix epoch timestamp representing when the file or directory was last accessed, or `none` if no such file or directory exists.


**Example:**
```tomo
assert (./file.txt).accessed() == Int64(1704221100)
assert (./not-a-file).accessed() == none

```
## Path.append

```tomo
Path.append : func(path: Path, text: Text, permissions: Int32 = Int32(0o644) -> Result)
```

Appends the given text to the file at the specified path, creating the file if it doesn't already exist. Failure to write will result in a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to append to.  | -
text | `Text` | The text to append to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is being created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./log.txt).append("extra line\n")!

```
## Path.append_bytes

```tomo
Path.append_bytes : func(path: Path, bytes: [Byte], permissions: Int32 = Int32(0o644) -> Result)
```

Appends the given bytes to the file at the specified path, creating the file if it doesn't already exist. Failure to write will result in a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to append to.  | -
bytes | `[Byte]` | The bytes to append to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is being created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./log.txt).append_bytes([104, 105])!

```
## Path.base_name

```tomo
Path.base_name : func(path: Path -> Text?)
```

Returns the base name of the file or directory at the specified path.

A POSIX filename is an arbitrary sequence of bytes, but `Text` holds Unicode, so this returns `none` when the name is not valid UTF-8.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** The base name of the file or directory, or `none` if it is not valid UTF-8.


**Example:**
```tomo
assert (./path/to/file.txt).base_name() == "file.txt"

```
## Path.by_line

```tomo
Path.by_line : func(path: Path -> func(->Text?)?)
```

Returns an iterator that can be used to iterate over a file one line at a time, or returns none if the file could not be opened.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -

**Return:** An iterator that can be used to get lines from a file one at a time or none if the file couldn't be read.


**Example:**
```tomo
# Safely handle file not being readable:
if lines := (./file.txt).by_line()
    for line in lines
        say(line.upper())
else
    say("Couldn't read file!")

# Assume the file is readable and error if that's not the case:
for line in (/dev/stdin).by_line()!
    say(line.upper())

```
## Path.byte_writer

```tomo
Path.byte_writer : func(path: Path, append: Bool = no, permissions: Int32 = Int32(0o644) -> func(bytes:[Byte], close:Bool=no -> Result))
```

Returns a function that can be used to repeatedly write bytes to the same file.

The file writer will keep its file descriptor open after each write (unless the `close` argument is set to `yes`). If the file writer is never closed, it will be automatically closed when the file writer is garbage collected.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
append | `Bool` | If set to `yes`, writes to the file will append. If set to `no`, then the first write to the file will overwrite its contents and subsequent calls will append.  | `no`
permissions | `Int32` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Returns a function that can repeatedly write bytes to the same file. If `close` is set to `yes`, then the file will be closed after writing. If this function is called again after closing, the file will be reopened for appending.


**Example:**
```tomo
write := (./file.txt).byte_writer()
write("Hello\n".utf8())!
write("world\n".utf8(), close=yes)!

```
## Path.bytes

```tomo
Path.bytes : func(path: Path -> [Byte])
```

Convert a path to a list of the raw bytes that make up its text representation. This does not read the file at the given path; see `Path.read_bytes` for that.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path.  | -

**Return:** A list of bytes (`[Byte]`) representing the path's text.


**Example:**
```tomo
assert (/foo/bar).bytes() == [47, 102, 111, 111, 47, 98, 97, 114]

```
## Path.can_execute

```tomo
Path.can_execute : func(path: Path -> Bool)
```

Returns whether or not a file can be executed by the current user/group.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to check.  | -

**Return:** `yes` if the file or directory exists and the current user has execute permissions, otherwise `no`.


**Example:**
```tomo
assert (/bin/sh).can_execute()
assert not (/usr/include/stdlib.h).can_execute()
assert not (/non/existant/file).can_execute()

```
## Path.can_read

```tomo
Path.can_read : func(path: Path -> Bool)
```

Returns whether or not a file can be read by the current user/group.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to check.  | -

**Return:** `yes` if the file or directory exists and the current user has read permissions, otherwise `no`.


**Example:**
```tomo
assert (/usr/include/stdlib.h).can_read()
assert not (/etc/shadow).can_read()
assert not (/non/existant/file).can_read()

```
## Path.can_write

```tomo
Path.can_write : func(path: Path -> Bool)
```

Returns whether or not a file can be written by the current user/group.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to check.  | -

**Return:** `yes` if the file or directory exists and the current user has write permissions, otherwise `no`.


**Example:**
```tomo
assert (/tmp).can_write()
assert not (/etc/passwd).can_write()
assert not (/non/existant/file).can_write()

```
## Path.changed

```tomo
Path.changed : func(path: Path, follow_symlinks: Bool = yes -> Int64?)
```

Gets the file change time of a file.

This is the ["ctime"](https://en.wikipedia.org/wiki/Stat_(system_call)#ctime) of a file, which is _not_ the file creation time.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file whose change time you want.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** A 64-bit unix epoch timestamp representing when the file or directory was last changed, or `none` if no such file or directory exists.


**Example:**
```tomo
assert (./file.txt).changed() == Int64(1704221100)
assert (./not-a-file).changed() == none

```
## Path.child

```tomo
Path.child : func(path: Path, child: Text -> Path)
```

Return a path that is a child of another path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of a directory.  | -
child | `Text` | The name of a child file or directory.  | -

**Return:** A new path representing the child.


**Example:**
```tomo
assert (./directory).child("file.txt") == (./directory/file.txt)

```
## Path.children

```tomo
Path.children : func(path: Path, include_hidden: Bool = no -> [Path]?)
```

Returns a list of children (files and directories) within the directory at the specified path. Optionally includes hidden files. Child ordering is not specified.

The paths returned keep the form of the path they came from: the children of `(./foo)` are relative and the children of `(~/foo)` stay home-based. Use `.resolved()` on them if you need absolute paths.

Returns `none` if the path is not a readable directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `Bool` | Whether to include hidden files (those starting with a `.`).  | `no`

**Return:** A list of paths for the children, or `none` if the directory could not be read. An existing but empty directory gives an empty list, not `none`.


**Example:**
```tomo
assert (./directory).children(include_hidden=yes)!.sorted() == [(./directory/.git), (./directory/foo.txt)]

# A directory that can't be read gives `none`, not an empty list:
assert (./not-a-directory).children() == none

```
## Path.components

```tomo
Path.components : func(path: Path -> [Text]?)
```

Returns a list of the file components of a path.

A POSIX filename is an arbitrary sequence of bytes, but `Text` holds Unicode, so this returns `none` when the name is not valid UTF-8.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** Each of the file components of the path in a list, or `none` if any component is not valid UTF-8. Note: for absolute paths, the first component will be "/". Trailing slashes are ignored.


**Example:**
```tomo
assert (./foo/baz.txt).components() == [".", "foo", "baz.txt"]
assert (/absolute/path/).components() == ["/", "absolute", "path"]

```
## Path.concatenated_with

```tomo
Path.concatenated_with : func(a: Path, b: Path -> Path)
```

Return the concatenation of two paths. This is what the `++` operator does on paths. The result is normalized, so `.` and `..` components in the second path are resolved away.

Argument | Type | Description | Default
---------|------|-------------|---------
a | `Path` | The base path.  | -
b | `Path` | A relative path to append. It is a runtime error if this is an absolute or home-based path.  | -

**Return:** The second path appended to the first.


**Example:**
```tomo
assert (/foo/bar).concatenated_with((./baz)) == (/foo/bar/baz)
assert ((/foo/bar) ++ (./baz/../qux)) == (/foo/bar/qux)

```
## Path.copy_to

```tomo
Path.copy_to : func(path: Path, dest: Path, overwrite: Bool = no -> Result)
```

Copies the file or directory from one location to another. This is the same behavior as `cp -r -T src dest` or `cp -rf -T src dest` (if `overwrite` is enabled).

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to copy.  | -
dest | `Path` | The destination to copy the path to.  | -
overwrite | `Bool` | Whether to permit overwriting the destination if it is an existing file or directory.  | `no`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./file.txt).copy_to(/tmp/copy.txt)!

```
## Path.create_directory

```tomo
Path.create_directory : func(path: Path, permissions: Int32 = Int32(0o755), recursive: Bool = yes -> Result)
```

Creates a new directory at the specified path with the given permissions. If any of the parent directories do not exist, they will be created as needed.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory to create.  | -
permissions | `Int32` | The permissions to set on the new directory.  | `Int32(0o755)`
recursive | `Bool` | If set to `yes`, then recursively create any parent directories if they don't exist, otherwise fail if the parent directory does not exist. When set to `yes`, this function behaves like `mkdir -p`.  | `yes`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./new_directory).create_directory()!

```
## Path.current_dir

```tomo
Path.current_dir : func(-> Path)
```

Returns the absolute path of the current working directory.


**Return:** The absolute path of the current directory.


**Example:**
```tomo
assert Path.current_dir() == (/home/user/tomo)

```
## Path.each_child

```tomo
Path.each_child : func(path: Path, include_hidden: Bool = no -> func(->Path?)?)
```

Returns an iterator over the children (files and directories) within the directory at the specified path. Optionally includes hidden files. Iteration order is not specified.

The paths returned keep the form of the path they came from: the children of `(./foo)` are relative and the children of `(~/foo)` stay home-based. Use `.resolved()` on them if you need absolute paths.

Returns `none` if the path is not a readable directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `Bool` | Whether to include hidden files (those starting with a `.`).  | `no`

**Return:** An iterator over the children in a directory, or `none` if the directory could not be read.


**Example:**
```tomo
# Safely handle the directory not being readable:
if children := (/dir).each_child()
    for child in children
        say("Child: $child")
else
    say("Couldn't read the directory!")

# Assume the directory is readable and error if that's not the case:
for child in (/dir).each_child()!
    say("Child: $child")

```
## Path.exists

```tomo
Path.exists : func(path: Path -> Bool)
```

Checks if a file or directory exists at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -

**Return:** `yes` if the file or directory exists, otherwise `no`.


**Example:**
```tomo
assert (/).exists()

```
## Path.expand_home

```tomo
Path.expand_home : func(path: Path -> Path)
```

For home-based paths (those starting with `~`), expand the path to replace the tilde with and absolute path to the user's `$HOME` directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to expand.  | -

**Return:** If the path does not start with a `~`, then return it unmodified. Otherwise, replace the `~` with an absolute path to the user's home directory.


**Example:**
```tomo
# Assume current user is 'user'
assert (~/foo).expand_home() == (/home/user/foo)
# No change
assert (/foo).expand_home() == (/foo)

```
## Path.extension

```tomo
Path.extension : func(path: Path, full: Bool = yes -> Text?)
```

Returns the file extension of the file at the specified path. Optionally returns the full extension.

A name with no `.` in it has no extension, and gives `none` rather than an empty text. A leading `.` does not count, so a dotfile like `(./.git)` has no extension, and neither does a name ending in a `.`.

Also returns `none` when the name is not valid UTF-8, a POSIX filename being an arbitrary sequence of bytes while `Text` holds Unicode.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -
full | `Bool` | Whether to return everything after the first `.` in the base name, or only the last part of the extension.  | `yes`

**Return:** The file extension, not including the leading `.`, or `none` if there is no extension.


**Example:**
```tomo
assert (./file.tar.gz).extension() == "tar.gz"
assert (./file.tar.gz).extension(full=no) == "gz"

# No "." in the name means no extension:
assert (/foo).extension() == none
assert (./.git).extension() == none
assert (./foo.).extension() == none

```
## Path.files

```tomo
Path.files : func(path: Path, include_hidden: Bool = no -> [Path]?)
```

Returns a list of files within the directory at the specified path. Optionally includes hidden files.

The paths returned keep the form of the path they came from: the children of `(./foo)` are relative and the children of `(~/foo)` stay home-based. Use `.resolved()` on them if you need absolute paths.

Returns `none` if the path is not a readable directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `Bool` | Whether to include hidden files (those starting with a `.`).  | `no`

**Return:** A list of file paths, or `none` if the directory could not be read. A directory with no files in it gives an empty list, not `none`.


**Example:**
```tomo
assert (./directory).files(include_hidden=yes)!.sorted() == [(./directory/file1.txt), (./directory/file2.txt)]

```
## Path.glob

```tomo
Path.glob : func(path: Path, pattern: Text -> [Path]?)
```

Search a directory for paths matching a glob pattern. The pattern is relative to the directory, and unlike `Path.matches_glob` it is anchored: it has to account for every component below the directory.

Details of the pattern syntax:
- `?`, `*`, and character classes like `[ch]` match within a single
  component and never across a `/`.

- Files or directories beginning with `.` will not match `*`, but will
  match `.*`. The paths "." and ".." are never included in the results.

- `**` stands for zero or more components, so `"**/*.txt"` finds `.txt`
  files at any depth. Unlike the other syntax, this cannot be answered by
  `glob(3)`, so such a pattern is served by walking the directory tree.
  `**` spans hidden components too, which is what makes it agree with
  `Path.matches_glob`. It does *not* descend through a symbolic link to a
  directory, since a link pointing back at an ancestor would never
  terminate; an explicit `*` component still matches one, so
  `"*/x.txt"` can find a file that `"**/x.txt"` will not.

- The `{a,b}` alternation syntax is not supported.
Results keep the form of the directory they came from, so globbing `(./src)` yields relative paths and globbing `(~/src)` yields home-based ones. Every path returned satisfies `Path.matches_glob` for the same pattern.

Returns `none` if the directory could not be read. A pattern that simply matches nothing gives an empty list, not `none`.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The directory to search.  | -
pattern | `Text` | The glob pattern to match against, relative to the directory. An empty pattern matches nothing.  | -

**Return:** A sorted list of the paths that match, or `none` if the directory could not be read.


**Example:**
```tomo
# A directory containing: foo.txt, baz.txt, qux.jpg, .hidden, sub/deep.txt
assert (./dir).glob("*.txt")! == [(./dir/baz.txt), (./dir/foo.txt)]
assert (./dir).glob(".*")! == [(./dir/.hidden)]
assert (./dir).glob("sub/*.txt")! == [(./dir/sub/deep.txt)]

# "**" is zero or more components:
assert (./dir).glob("**/*.txt")! == [(./dir/baz.txt), (./dir/foo.txt), (./dir/sub/deep.txt)]

# A pattern matching nothing is an empty list:
assert (./dir).glob("*.xxx")! == []

# A directory that can't be read is none:
assert (./not-a-directory).glob("*") == none

```
## Path.group

```tomo
Path.group : func(path: Path, follow_symlinks: Bool = yes -> Text?)
```

Get the owning group of a file or directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path whose owning group to get.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** The name of the group which owns the file or directory, or `none` if the path does not exist.


**Example:**
```tomo
assert (/bin).group() == "root"
assert (/non/existent/file).group() == none

```
## Path.has_extension

```tomo
Path.has_extension : func(path: Path, extension: Text -> Bool)
```

Return whether or not a path has a given file extension. Unlike `Path.extension`, this reads the raw bytes of the name and so works on names that are not valid UTF-8.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
extension | `Text` | A file extension (leading `.` is optional). If empty, the check will test if the file does not have any file extension.  | -

**Return:** Whether or not the path has the given extension.


**Example:**
```tomo
assert (/foo.txt).has_extension("txt")
assert (/foo.txt).has_extension(".txt")
assert (/foo.tar.gz).has_extension("gz")
assert not (/foo.tar.gz).has_extension("zip")

# Asking for an empty extension is the same question as `.extension()`
# being `none`, for any name that is valid UTF-8:
assert (/foo).has_extension("")
assert (/).has_extension("")

```
## Path.is_directory

```tomo
Path.is_directory : func(path: Path, follow_symlinks: Bool = yes -> Bool)
```

Checks if the path represents a directory. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** `yes` if the path is a directory, otherwise `no`.


**Example:**
```tomo
assert (./directory/).is_directory()
assert not (./file.txt).is_directory()

```
## Path.is_file

```tomo
Path.is_file : func(path: Path, follow_symlinks: Bool = yes -> Bool)
```

Checks if the path represents a file. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** `yes` if the path is a file, otherwise `no`.


**Example:**
```tomo
assert (./file.txt).is_file()
assert not (./directory/).is_file()

```
## Path.is_pipe

```tomo
Path.is_pipe : func(path: Path, follow_symlinks: Bool = yes -> Bool)
```

Checks if the path represents a named pipe (a FIFO). Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** `yes` if the path is a named pipe, otherwise `no`.


**Example:**
```tomo
assert (./my-fifo).is_pipe()
assert not (./file.txt).is_pipe()

```
## Path.is_socket

```tomo
Path.is_socket : func(path: Path, follow_symlinks: Bool = yes -> Bool)
```

Checks if the path represents a socket. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** `yes` if the path is a socket, otherwise `no`.


**Example:**
```tomo
assert (./socket).is_socket()

```
## Path.is_symlink

```tomo
Path.is_symlink : func(path: Path -> Bool)
```

Checks if the path represents a symbolic link.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -

**Return:** `yes` if the path is a symbolic link, otherwise `no`.


**Example:**
```tomo
assert (./link).is_symlink()

```
## Path.lines

```tomo
Path.lines : func(path: Path -> [Text]?)
```

Returns a list with the lines of text in a file or returns none if the file could not be opened.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -

**Return:** A list of the lines in a file or none if the file couldn't be read.


**Example:**
```tomo
lines := (./file.txt).lines()!

```
## Path.matches_glob

```tomo
Path.matches_glob : func(path: Path, glob: Text -> Bool)
```

Return whether or not a path matches a given glob. This is a pattern match, not a filesystem operation, and it reads the raw bytes of the path, so it works on names that are not valid UTF-8.

The pattern is split on `/` and matched against the path's components from the back, so a pattern matches any trailing run of components:
- `"*.txt"` asks about the file's name wherever it sits, so it matches
  `(./foo.txt)`, `(/tmp/dir/foo.txt)`, and `(~/foo.txt)` alike. The answer
  does not depend on which form the path was written in.

- `"dir/*.txt"` matches a `.txt` file directly inside any `dir`.
- A pattern beginning with `/` can only line up at the front of the path,
  so `"/tmp/*.txt"` pins the whole path rather than matching a suffix. A
  pattern beginning with `./` likewise only matches `./`-form paths.

- `**` stands for zero or more components, so `"**/*.txt"` matches a `.txt`
  file at any depth, including none.

- `?`, `*`, and character classes like `[ch]` match within a single
  component and never across a `/`. A `.` at the start of a component must
  be matched by a literal `.`, so `*` does not match hidden files.

- The `{a,b}` alternation syntax is not supported.

An empty pattern matches nothing.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
glob | `Text` | The glob pattern to check.  | -

**Return:** Whether or not the path matches the given glob.


**Example:**
```tomo
# A bare pattern asks about the file's name, whatever form the path is in:
assert (./file.txt).matches_glob("*.txt")
assert (/tmp/dir/file.txt).matches_glob("*.txt")
assert not (./file.txt).matches_glob("*.jpg")

# More components match a longer suffix:
assert (./src/file.c).matches_glob("src/*.[ch]")
assert (./src/file.c).matches_glob("./src/*.[ch]")

# A leading "/" anchors the pattern to the whole path:
assert (/tmp/dir/file.txt).matches_glob("/tmp/dir/*.txt")
assert not (/other/dir/file.txt).matches_glob("/tmp/dir/*.txt")

# "**" is zero or more components:
assert (./a/b/c/file.txt).matches_glob("**/*.txt")
assert (./file.txt).matches_glob("**/*.txt")

# "*" does not match a leading "." or cross a "/":
assert not (./dir/.hidden).matches_glob("*")
assert (./dir/.hidden).matches_glob(".*")

```
## Path.modified

```tomo
Path.modified : func(path: Path, follow_symlinks: Bool = yes -> Int64?)
```

Gets the file modification time of a file.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file whose modification time you want.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** A 64-bit unix epoch timestamp representing when the file or directory was last modified, or `none` if no such file or directory exists.


**Example:**
```tomo
assert (./file.txt).modified() == Int64(1704221100)
assert (./not-a-file).modified() == none

```
## Path.move

```tomo
Path.move : func(path: Path, dest: Path, overwrite: Bool = no -> Result)
```

Moves the file or directory from one location to another.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to move.  | -
dest | `Path` | The destination to move the path to.  | -
overwrite | `Bool` | Whether to permit overwriting the destination if it is an existing file or directory.  | `no`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./file.txt).move(/tmp/renamed.txt)!

```
## Path.owner

```tomo
Path.owner : func(path: Path, follow_symlinks: Bool = yes -> Text?)
```

Get the owning user of a file or directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path whose owner to get.  | -
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** The name of the user who owns the file or directory, or `none` if the path does not exist.


**Example:**
```tomo
assert (/bin).owner() == "root"
assert (/non/existent/file).owner() == none

```
## Path.parent

```tomo
Path.parent : func(path: Path -> Path?)
```

Returns the parent directory of the file or directory at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** The path of the parent directory or `none` if the path is `(/)` (the file root).


**Example:**
```tomo
assert (./path/to/file.txt).parent() == (./path/to)

```
## Path.read

```tomo
Path.read : func(path: Path -> Text?)
```

Reads the contents of the file at the specified path or none if the file could not be read.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to read.  | -

**Return:** The contents of the file. If the file could not be read, none will be returned. If the file can be read, but is not valid UTF8 data, an error will be raised.


**Example:**
```tomo
assert (./hello.txt).read() == "Hello"
assert (./nosuchfile.xxx).read() == none

```
## Path.read_bytes

```tomo
Path.read_bytes : func(path: Path, limit: Int? = none -> [Byte]?)
```

Reads the contents of the file at the specified path or none if the file could not be read.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to read.  | -
limit | `Int?` | A limit to how many bytes should be read.  | `none`

**Return:** The byte contents of the file. If the file cannot be read, none will be returned.


**Example:**
```tomo
assert (./hello.txt).read_bytes()! == [72, 101, 108, 108, 111]
assert (./nosuchfile.xxx).read_bytes() == none

```
## Path.relative_to

```tomo
Path.relative_to : func(path: Path, relative_to: Path -> Path)
```

Returns the path relative to a given base path.

The result has no leading `./`, so a path naming something inside the base path cannot be written as a path literal. Results that climb out of the base path begin with `../` and can.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to convert.  | -
relative_to | `Path` | The base path for the relative path. Unlike `Path.resolved`, this argument is required.  | -

**Return:** A relative path from the reference point to the given path.


**Example:**
```tomo
# A path relative to itself is the current directory:
assert (/tmp).relative_to((/tmp)) == (.)
assert "$((./path/to/file.txt).relative_to((./path)))" == "to/file.txt"
assert "$((/tmp/foo).relative_to((/tmp)))" == "foo"
assert (/a/b/c).relative_to((/a/x)) == (../b/c)

```
## Path.remove

```tomo
Path.remove : func(path: Path, ignore_missing: Bool = no -> Result)
```

Removes the file or directory at the specified path. A runtime error is raised if something goes wrong.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to remove.  | -
ignore_missing | `Bool` | Whether to ignore errors if the file or directory does not exist.  | `no`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./file.txt).remove()!

```
## Path.resolved

```tomo
Path.resolved : func(path: Path, relative_to: Path = (./) -> Path)
```

Resolves the absolute path of the given path relative to a base path. By default, the base path is the current directory. A relative base is itself resolved against the current directory first, so the result is always absolute.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to resolve.  | -
relative_to | `Path` | The base path for resolution.  | `(./)`

**Return:** The resolved absolute path.


**Example:**
```tomo
# Assume the current directory is /home/user
assert (~/foo).resolved() == (/home/user/foo)
assert (./foo).resolved() == (/home/user/foo)
assert (./path/to/file.txt).resolved(relative_to=(/foo)) == (/foo/path/to/file.txt)

```
## Path.set_owner

```tomo
Path.set_owner : func(path: Path, owner: Text? = none, group: Text? = none, follow_symlinks: Bool = yes -> Result)
```

Set the owning user and/or group for a path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to change the permissions for.  | -
owner | `Text?` | If non-none, the new user to assign to be the owner of the file.  | `none`
group | `Text?` | If non-none, the new group to assign to be the owner of the file.  | `none`
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./file.txt).set_owner(owner="root", group="wheel")!

```
## Path.sibling

```tomo
Path.sibling : func(path: Path, name: Text -> Path?)
```

Return a path that is a sibling of another path (i.e. has the same parent, but a different name). This is equivalent to `.parent().child(name)`

Returns `none` for a path with no parent to put a sibling beside, which is the file root `(/)`.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
name | `Text` | The name of a sibling file or directory.  | -

**Return:** A new path representing the sibling, or `none` if the path is `(/)`.


**Example:**
```tomo
assert (/foo/baz).sibling("doop") == (/foo/doop)
assert (/).sibling("doop") == none

```
## Path.subdirectories

```tomo
Path.subdirectories : func(path: Path, include_hidden: Bool = no -> [Path]?)
```

Returns a list of subdirectories within the directory at the specified path. Optionally includes hidden subdirectories.

The paths returned keep the form of the path they came from: the children of `(./foo)` are relative and the children of `(~/foo)` stay home-based. Use `.resolved()` on them if you need absolute paths.

Returns `none` if the path is not a readable directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `Bool` | Whether to include hidden subdirectories (those starting with a `.`)  | `no`

**Return:** A list of subdirectory paths, or `none` if the directory could not be read. A directory with no subdirectories gives an empty list, not `none`.


**Example:**
```tomo
assert (./directory).subdirectories()!.sorted() == [(./directory/subdir1), (./directory/subdir2)]
assert (./directory).subdirectories(include_hidden=yes)!.sorted() == [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]

```
## Path.unique_directory

```tomo
Path.unique_directory : func(path: Path -> Path?)
```

Generates a unique directory path based on the given path. Useful for creating temporary directories.

Returns `none` if the directory could not be created.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique directory. The last six letters of this path must be `XXXXXX`.  | -

**Return:** A unique directory path after creating the directory, or `none` if it could not be created.


**Example:**
```tomo
created := (/tmp/my-dir.XXXXXX).unique_directory()!
assert created.is_directory()
created.remove()!

```
## Path.walk

```tomo
Path.walk : func(path: Path, include_hidden: Bool = no, follow_symlinks: Bool = no -> func(->Path?)?)
```

Returns an iterator that efficiently recursively walks over every file and subdirectory in a given directory. The iteration order is not defined, but in practice it may look a lot like a breadth-first traversal.

The path itself is always included in the iteration. The paths returned keep the form of the path they came from: walking `(./foo)` yields relative paths and walking `(~/foo)` yields home-based ones.

Returns `none` if there is nothing at the given path. A path that exists but is not a directory walks over just itself.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to begin the walk.  | -
include_hidden | `Bool` | Whether to include hidden files (those starting with a `.`)  | `no`
follow_symlinks | `Bool` | Whether to follow symbolic links. Caution: if set to 'yes', it is possible for this iterator to get stuck in a loop, using increasingly large amounts of memory.  | `no`

**Return:** An iterator that recursively walks over every file and subdirectory, or `none` if the path does not exist.


**Example:**
```tomo
for p in (/tmp).walk()!
    say("File or dir: $p")

# The path itself is always included:
assert [p for p in (./file.txt).walk()!] == [(./file.txt)]

# A path that isn't there has nothing to walk:
assert (./not-a-path).walk() == none

```
## Path.with_extension

```tomo
Path.with_extension : func(path: Path, extension: Text, replace: Bool = yes -> Path)
```

Return a path with the given file extension, either replacing the path's existing extension or adding to it.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
extension | `Text` | The file extension to apply (a leading `.` is optional). If empty, the extension is removed entirely.  | -
replace | `Bool` | If `yes`, the path's existing extension is replaced. If `no`, the new extension is appended to the existing one.  | `yes`

**Return:** A new path with the requested extension.


**Example:**
```tomo
assert (./file.tar.gz).with_extension("zip") == (./file.zip)
assert (./file.tar.gz).with_extension(".zip") == (./file.zip)
assert (./file.tar.gz).with_extension("") == (./file)
assert (./file.tar.gz).with_extension("zip", replace=no) == (./file.tar.gz.zip)
assert (./file).with_extension("txt") == (./file.txt)

```
## Path.write

```tomo
Path.write : func(path: Path, text: Text, permissions: Int32 = Int32(0o644) -> Result)
```

Writes the given text to the file at the specified path, creating the file if it doesn't already exist. Sets the file permissions as specified. If the file writing cannot be successfully completed, a runtime error is raised.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
text | `Text` | The text to write to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./file.txt).write("Hello, world!")!

```
## Path.write_bytes

```tomo
Path.write_bytes : func(path: Path, bytes: [Byte], permissions: Int32 = Int32(0o644) -> Result)
```

Writes the given bytes to the file at the specified path, creating the file if it doesn't already exist. Sets the file permissions as specified. If the file writing cannot be successfully completed, a runtime error is raised.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
bytes | `[Byte]` | A list of bytes to write to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Either `Success` or `Failure{reason}`.


**Example:**
```tomo
(./file.txt).write_bytes([104, 105])!

```
## Path.write_unique

```tomo
Path.write_unique : func(path: Path, text: Text -> Path?)
```

Writes the given text to a unique file path based on the specified path. The file is created if it doesn't exist. This is useful for creating temporary files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique file. This path must include the string `XXXXXX` in the file base name.  | -
text | `Text` | The text to write to the file.  | -

**Return:** The path of the newly created unique file, or `none` if the file could not be created.


**Example:**
```tomo
# The created path has the XXXXXX replaced by random characters,
# e.g. (./file-27QHtq.txt):
created := (./file-XXXXXX.txt).write_unique("Hello, world!")!
assert created.read()! == "Hello, world!"
created.remove()!

```
## Path.write_unique_bytes

```tomo
Path.write_unique_bytes : func(path: Path, bytes: [Byte] -> Path?)
```

Writes the given bytes to a unique file path based on the specified path. The file is created if it doesn't exist. This is useful for creating temporary files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique file. This path must include the string `XXXXXX` in the file base name.  | -
bytes | `[Byte]` | The bytes to write to the file.  | -

**Return:** The path of the newly created unique file, or `none` if the file could not be created.


**Example:**
```tomo
# The created path has the XXXXXX replaced by random characters,
# e.g. (./file-27QHtq.txt):
created := (./file-XXXXXX.txt).write_unique_bytes([1, 2, 3])!
assert created.read_bytes()! == [1, 2, 3]
created.remove()!

```
## Path.writer

```tomo
Path.writer : func(path: Path, append: Bool = no, permissions: Int32 = Int32(0o644) -> func(text:Text, close:Bool=no -> Result))
```

Returns a function that can be used to repeatedly write to the same file.

The file writer will keep its file descriptor open after each write (unless the `close` argument is set to `yes`). If the file writer is never closed, it will be automatically closed when the file writer is garbage collected.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
append | `Bool` | If set to `yes`, writes to the file will append. If set to `no`, then the first write to the file will overwrite its contents and subsequent calls will append.  | `no`
permissions | `Int32` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Returns a function that can repeatedly write to the same file. If `close` is set to `yes`, then the file will be closed after writing. If this function is called again after closing, the file will be reopened for appending.


**Example:**
```tomo
write := (./file.txt).writer()
write("Hello\n")!
write("world\n", close=yes)!

```
