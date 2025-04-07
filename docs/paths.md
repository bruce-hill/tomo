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
>> (/tmp)
= (/tmp)
>> (~/path with/(parens) is/ok/)
= (~/path with/(parens) is/ok/)
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

## Path Methods

- [`func accessed(path:Path, follow_symlinks=yes -> Int64?)`](#accessed)
- [`func append(path: Path, text: Text, permissions: Int32 = 0o644[32] -> Void)`](#append)
- [`func append_bytes(path: Path, bytes: [Byte], permissions: Int32 = 0o644[32] -> Void)`](#append_bytes)
- [`func base_name(path: Path -> Text)`](#base_name)
- [`func by_line(path: Path -> func(->Text?)?)`](#by_line)
- [`func can_execute(path:Path -> Bool)`](#can_execute)
- [`func can_read(path:Path -> Bool)`](#can_read)
- [`func can_write(path:Path -> Bool)`](#can_write)
- [`func changed(path:Path, follow_symlinks=yes -> Int64?)`](#changed)
- [`func child(path: Path, child:Text -> Path)`](#child)
- [`func children(path: Path, include_hidden=no -> [Path])`](#children)
- [`func create_directory(path: Path, permissions=0o755[32] -> Void)`](#create_directory)
- [`func exists(path: Path -> Bool)`](#exists)
- [`func expand_home(path: Path -> Path)`](#expand_home)
- [`func extension(path: Path, full=yes -> Text)`](#extension)
- [`func files(path: Path, include_hidden=no -> [Path])`](#files)
- [`func from_components(components:[Text] -> Path)`](#from_components)
- [`func glob(path: Path -> [Path])`](#glob)
- [`func group(path: Path, follow_symlinks=yes -> Text?)`](#group)
- [`func is_directory(path: Path, follow_symlinks=yes -> Bool)`](#is_directory)
- [`func is_file(path: Path, follow_symlinks=yes -> Bool)`](#is_file)
- [`func is_socket(path: Path, follow_symlinks=yes -> Bool)`](#is_socket)
- [`func is_symlink(path: Path -> Bool)`](#is_symlink)
- [`func modified(path:Path, follow_symlinks=yes -> Int64?)`](#modified)
- [`func owner(path: Path, follow_symlinks=yes -> Text?)`](#owner)
- [`func parent(path: Path -> Path)`](#parent)
- [`func read(path: Path -> Text?)`](#read)
- [`func read_bytes(path: Path, limit: Int? = none -> [Byte]?)`](#read_bytes)
- [`func relative_to(path: Path, relative_to=(./) -> Path)`](#relative_to)
- [`func remove(path: Path, ignore_missing=no -> Void)`](#remove)
- [`func resolved(path: Path, relative_to=(./) -> Path)`](#resolved)
- [`func set_owner(path:Path, owner:Text?=none, group:Text?=none, follow_symlinks=yes)`](#set_owner)
- [`func subdirectories(path: Path, include_hidden=no -> [Path])`](#subdirectories)
- [`func unique_directory(path: Path -> Path)`](#unique_directory)
- [`func write(path: Path, text: Text, permissions=0o644[32] -> Void)`](#write)
- [`func write_bytes(path: Path, bytes: [Byte], permissions=0o644[32] -> Void)`](#write_bytes)
- [`func write_unique(path: Path, text: Text -> Path)`](#write_unique)
- [`func write_unique_bytes(path: Path, bytes: [Byte] -> Path)`](#write_unique_bytes)

### `accessed`
Gets the file access time of a file.

```tomo
func accessed(path:Path, follow_symlinks: Bool = yes -> Int64?)
```

- `path`: The path of the file whose access time you want.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
A 64-bit unix epoch timestamp representing when the file or directory was last
accessed, or `none` if no such file or directory exists.

**Example:**  
```tomo
>> (./file.txt).accessed()
= 1704221100?
>> (./not-a-file).accessed()
= none
```

---

### `append`
Appends the given text to the file at the specified path, creating the file if
it doesn't already exist. Failure to write will result in a runtime error.

```tomo
func append(path: Path, text: Text, permissions: Int32 = 0o644[32] -> Void)
```

- `path`: The path of the file to append to.
- `text`: The text to append to the file.
- `permissions`: The permissions to set on the file if it is being created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```tomo
(./log.txt).append("extra line$(\n)")
```

---

### `append_bytes`
Appends the given bytes to the file at the specified path, creating the file if
it doesn't already exist. Failure to write will result in a runtime error.

```tomo
func append_bytes(path: Path, bytes: [Byte], permissions: Int32 = 0o644[32] -> Void)
```

- `path`: The path of the file to append to.
- `bytes`: The bytes to append to the file.
- `permissions`: The permissions to set on the file if it is being created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```tomo
(./log.txt).append_bytes([104[B], 105[B]])
```

---

### `base_name`
Returns the base name of the file or directory at the specified path.

```tomo
func base_name(path: Path -> Text)
```

- `path`: The path of the file or directory.

**Returns:**  
The base name of the file or directory.

**Example:**  
```tomo
>> (./path/to/file.txt).base_name()
= "file.txt"
```

---

### `by_line`
Returns an iterator that can be used to iterate over a file one line at a time,
or returns a null value if the file could not be opened.

```tomo
func by_line(path: Path -> func(->Text?)?)
```

- `path`: The path of the file.

**Returns:**  
An iterator that can be used to get lines from a file one at a time or a null
value if the file couldn't be read.

**Example:**  
```tomo
# Safely handle file not being readable:
if lines := (./file.txt).by_line():
    for line in lines:
        say(line.upper())
else:
    say("Couldn't read file!")

# Assume the file is readable and error if that's not the case:
for line in (/dev/stdin).by_line()!:
    say(line.upper())
```

---

### `can_execute`
Returns whether or not a file can be executed by the current user/group.

```tomo
func can_execute(path: Path -> Bool)
```

- `path`: The path of the file to check.

**Returns:**
`yes` if the file or directory exists and the current user has execute permissions, otherwise `no`.

**Example:**  
```tomo
>> (/bin/sh).can_execute()
= yes
>> (/usr/include/stdlib.h).can_execute()
= no
>> (/non/existant/file).can_execute()
= no
```

---

### `can_read`
Returns whether or not a file can be read by the current user/group.

```tomo
func can_read(path: Path -> Bool)
```

- `path`: The path of the file to check.

**Returns:**
`yes` if the file or directory exists and the current user has read permissions, otherwise `no`.

**Example:**  
```tomo
>> (/usr/include/stdlib.h).can_read()
= yes
>> (/etc/shadow).can_read()
= no
>> (/non/existant/file).can_read()
= no
```

---

### `can_write`
Returns whether or not a file can be written by the current user/group.

```tomo
func can_write(path: Path -> Bool)
```

- `path`: The path of the file to check.

**Returns:**
`yes` if the file or directory exists and the current user has write permissions, otherwise `no`.

**Example:**  
```tomo
>> (/tmp).can_write()
= yes
>> (/etc/passwd).can_write()
= no
>> (/non/existant/file).can_write()
= no
```

---

### `changed`
Gets the file change time of a file.

**Note:** this is the
["ctime"](https://en.wikipedia.org/wiki/Stat_(system_call)#ctime) of a file,
which is _not_ the file creation time.

```tomo
func changed(path:Path, follow_symlinks: Bool = yes -> Int64?)
```

- `path`: The path of the file whose change time you want.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
A 64-bit unix epoch timestamp representing when the file or directory was last
changed, or `none` if no such file or directory exists.

**Example:**  
```tomo
>> (./file.txt).changed()
= 1704221100?
>> (./not-a-file).changed()
= none
```

---

### `child`
Return a path that is a child of another path.

```tomo
func child(path: Path, child: Text -> [Path])
```

- `path`: The path of a directory.
- `child`: The name of a child file or directory.

**Returns:**  
A new path representing the child.

**Example:**  
```tomo
>> (./directory).child("file.txt")
= (./directory/file.txt)
```

---

### `children`
Returns a list of children (files and directories) within the directory at the specified path. Optionally includes hidden files.

```tomo
func children(path: Path, include_hidden=no -> [Path])
```

- `path`: The path of the directory.
- `include_hidden`: Whether to include hidden files, which start with a `.` (default is `no`).

**Returns:**  
A list of paths for the children.

**Example:**  
```tomo
>> (./directory).children(include_hidden=yes)
= [".git", "foo.txt"]
```

---

### `create_directory`
Creates a new directory at the specified path with the given permissions. If
any of the parent directories do not exist, they will be created as needed.

```tomo
func create_directory(path: Path, permissions=0o755[32] -> Void)
```

- `path`: The path of the directory to create.
- `permissions`: The permissions to set on the new directory (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```tomo
(./new_directory).create_directory()
```

---

### `exists`
Checks if a file or directory exists at the specified path.

```tomo
func exists(path: Path -> Bool)
```

- `path`: The path to check.

**Returns:**  
`True` if the file or directory exists, `False` otherwise.

**Example:**  
```tomo
>> (/).exists()
= yes
```

---

### `expand_home`
For home-based paths (those starting with `~`), expand the path to replace the
tilde with and absolute path to the user's `$HOME` directory.

```tomo
func expand_home(path: Path -> Path)
```

- `path`: The path to expand.

**Returns:**  
If the path does not start with a `~`, then return it unmodified. Otherwise,
replace the `~` with an absolute path to the user's home directory.

**Example:**  
```tomo
>> (~/foo).expand_home() # Assume current user is 'user'
= /home/user/foo
>> (/foo).expand_home() # No change
= /foo
```

---

### `extension`
Returns the file extension of the file at the specified path. Optionally returns the full extension.

```tomo
func extension(path: Path, full:Bool = yes -> Text)
```

- `path`: The path of the file.
- `full`: Whether to return everything after the first `.` in the
  base name, or only the last part of the extension (default is `yes`).

**Returns:**  
The file extension (not including the leading `.`) or an empty text if there is
no file extension.

**Example:**  
```tomo
>> (./file.tar.gz).extension()
= "tar.gz"
>> (./file.tar.gz).extension(full=no)
= "gz"
>> (/foo).extension()
= ""
>> (./.git).extension()
= ""
```

---

### `files`
Returns a list of files within the directory at the specified path. Optionally includes hidden files.

```tomo
func files(path: Path, include_hidden: Bool = no -> [Path])
```

- `path`: The path of the directory.
- `include_hidden`: Whether to include hidden files (default is `no`).

**Returns:**  
A list of file paths.

**Example:**  
```tomo
>> (./directory).files(include_hidden=yes)
= [(./directory/file1.txt), (./directory/file2.txt)]
```

---

### `from_components`
Returns a path built from a list of path components.

```tomo
func from_components(components: [Text] -> Path)
```

- `components`: A list of path components.

**Returns:**  
A path representing the given components.

**Example:**  
```tomo
>> Path.from_components(["/", "usr", "include"])
= /usr/include
>> Path.from_components(["foo.txt"])
= ./foo.txt
>> Path.from_components(["~", ".local"])
= ~/.local
```

---

### `glob`
Perform a globbing operation and return a list of matching paths. Some glob
specific details:

- The paths "." and ".." are *not* included in any globbing results.
- Files or directories that begin with "." will not match `*`, but will match `.*`.
- Globs do support `{a,b}` syntax for matching files that match any of several
  choices of patterns.
- The shell-style syntax `**` for matching subdirectories is not supported.

```tomo
func glob(path: Path -> [Path])
```

- `path`: The path of the directory which may contain special globbing characters
  like `*`, `?`, or `{...}`

**Returns:**  
A list of file paths that match the glob.

**Example:**  
```tomo
# Current directory includes: foo.txt, baz.txt, qux.jpg, .hidden
>> (./*).glob()
= [(./foo.txt), (./baz.txt), (./qux.jpg)]

>> (./*.txt).glob()
= [(./foo.txt), (./baz.txt)]

>> (./*.{txt,jpg}).glob()
= [(./foo.txt), (./baz.txt), (./qux.jpg)]

>> (./.*).glob()
= [(./.hidden)]

# Globs with no matches return an empty list:
>> (./*.xxx).glob()
= []
```

---

### `group`
Get the owning group of a file or directory.

```tomo
func group(path: Path, follow_symlinks: Bool = yes -> Text?)
```

- `path`: The path whose owning group to get.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
The name of the group which owns the file or directory, or `none` if the path does not exist.

**Example:**  
```tomo
>> (/bin).group()
= "root"
>> (/non/existent/file).group()
= none
```

---

### `is_directory`
Checks if the path represents a directory. Optionally follows symbolic links.

```tomo
func is_directory(path: Path, follow_symlinks=yes -> Bool)
```

- `path`: The path to check.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
`True` if the path is a directory, `False` otherwise.

**Example:**  
```tomo
>> (./directory/).is_directory()
= yes

>> (./file.txt).is_directory()
= no
```

---

### `is_file`
Checks if the path represents a file. Optionally follows symbolic links.

```tomo
func is_file(path: Path, follow_symlinks=yes -> Bool)
```

- `path`: The path to check.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
`True` if the path is a file, `False` otherwise.

**Example:**  
```tomo
>> (./file.txt).is_file()
= yes

>> (./directory/).is_file()
= no
```

---

### `is_socket`
Checks if the path represents a socket. Optionally follows symbolic links.

```tomo
func is_socket(path: Path, follow_symlinks=yes -> Bool)
```

- `path`: The path to check.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
`True` if the path is a socket, `False` otherwise.

**Example:**  
```tomo
>> (./socket).is_socket()
= yes
```

---

### `is_symlink`
Checks if the path represents a symbolic link.

```tomo
func is_symlink(path: Path -> Bool)
```

- `path`: The path to check.

**Returns:**  
`True` if the path is a symbolic link, `False` otherwise.

**Example:**  
```tomo
>> (./link).is_symlink()
= yes
```

---

### `modified`
Gets the file modification time of a file.

```tomo
func modified(path:Path, follow_symlinks: Bool = yes -> Int64?)
```

- `path`: The path of the file whose modification time you want.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
A 64-bit unix epoch timestamp representing when the file or directory was last
modified, or `none` if no such file or directory exists.

**Example:**  
```tomo
>> (./file.txt).modified()
= 1704221100?
>> (./not-a-file).modified()
= none
```

---

### `owner`
Get the owning user of a file or directory.

```tomo
func owner(path: Path, follow_symlinks: Bool = yes -> Text?)
```

- `path`: The path whose owner to get.
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
The name of the user who owns the file or directory, or `none` if the path does not exist.

**Example:**  
```tomo
>> (/bin).owner()
= "root"
>> (/non/existent/file).owner()
= none
```

---

### `parent`
Returns the parent directory of the file or directory at the specified path.

```tomo
func parent(path: Path -> Path)
```

- `path`: The path of the file or directory.

**Returns:**  
The path of the parent directory.

**Example:**  
```tomo
>> (./path/to/file.txt).parent()
= (./path/to/)
```

---

### `read`
Reads the contents of the file at the specified path or a null value if the
file could not be read.

```tomo
func read(path: Path -> Text?)
```

- `path`: The path of the file to read.

**Returns:**  
The contents of the file. If the file could not be read, a null value will be
returned. If the file can be read, but is not valid UTF8 data, an error will be
raised.

**Example:**  
```tomo
>> (./hello.txt).read()
= "Hello"?

>> (./nosuchfile.xxx).read()
= none
```
---

### `read_bytes`
Reads the contents of the file at the specified path or a null value if the
file could not be read.

```tomo
func read_bytes(path: Path, limit: Int? = none -> [Byte]?)
```

- `path`: The path of the file to read.
- `limit`: A limit to how many bytes should be read.

**Returns:**  
The byte contents of the file. If the file cannot be read, a null value will be
returned.

**Example:**  
```tomo
>> (./hello.txt).read()
= [72[B], 101[B], 108[B], 108[B], 111[B]]?

>> (./nosuchfile.xxx).read()
= none
```

---

### `relative_to`
Returns the path relative to a given base path. By default, the base path is the current directory.

```tomo
func relative_to(path: Path, relative_to=(./) -> Path)
```

- `path`: The path to convert.
- `relative_to`: The base path for the relative path (default is `./`).

**Returns:**  
The relative path.

**Example:**  
```tomo
>> (./path/to/file.txt).relative(relative_to=(./path))
= (./to/file.txt)
```

---

### `remove`
Removes the file or directory at the specified path. A runtime error is raised if something goes wrong.

```tomo
func remove(path: Path, ignore_missing=no -> Void)
```

- `path`: The path to remove.
- `ignore_missing`: Whether to ignore errors if the file or directory does not exist (default is `no`).

**Returns:**  
Nothing.

**Example:**  
```tomo
(./file.txt).remove()
```

---

### `resolved`
Resolves the absolute path of the given path relative to a base path. By default, the base path is the current directory.

```tomo
func resolved(path: Path, relative_to=(./) -> Path)
```

- `path`: The path to resolve.
- `relative_to`: The base path for resolution (default is `./`).

**Returns:**  
The resolved absolute path.

**Example:**  
```tomo
>> (~/foo).resolved()
= (/home/user/foo)

>> (./path/to/file.txt).resolved(relative_to=(/foo))
= (/foo/path/to/file.txt)
```

---

### `set_owner`
Set the owning user and/or group for a path.

```tomo
func set_owner(path:Path, owner: Text? = none, group: Text? = none, follow_symlinks: Bool = yes)
```

- `path`: The path to change the permissions for.
- `owner`: If non-none, the new user to assign to be the owner of the file (default: `none`).
- `group`: If non-none, the new group to assign to be the owner of the file (default: `none`).
- `follow_symlinks`: Whether to follow symbolic links (default is `yes`).

**Returns:**  
Nothing. If a path does not exist, a failure will be raised.

**Example:**  
```tomo
(./file.txt).set_owner(owner="root", group="wheel")
```

---

### `subdirectories`
Returns a list of subdirectories within the directory at the specified path. Optionally includes hidden subdirectories.

```tomo
func subdirectories(path: Path, include_hidden=no -> [Path])
```

- `path`: The path of the directory.
- `include_hidden`: Whether to include hidden subdirectories (default is `no`).

**Returns:**  
A list of subdirectory paths.

**Example:**  
```tomo
>> (./directory).subdirectories()
= [(./directory/subdir1), (./directory/subdir2)]

>> (./directory).subdirectories(include_hidden=yes)
= [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]
```

---

### `unique_directory`
Generates a unique directory path based on the given path. Useful for creating temporary directories.

```tomo
func unique_directory(path: Path -> Path)
```

- `path`: The base path for generating the unique directory. The last six letters of this path must be `XXXXXX`.

**Returns:**  
A unique directory path after creating the directory.

**Example:**  

```
>> created := (/tmp/my-dir.XXXXXX).unique_directory()
= (/tmp/my-dir-AwoxbM/)
>> created.is_directory()
= yes
created.remove()
```

---

### `write`
Writes the given text to the file at the specified path, creating the file if
it doesn't already exist. Sets the file permissions as specified. If the file
writing cannot be successfully completed, a runtime error is raised.

```tomo
func write(path: Path, text: Text, permissions=0o644[32] -> Void)
```

- `path`: The path of the file to write to.
- `text`: The text to write to the file.
- `permissions`: The permissions to set on the file if it is created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```tomo
(./file.txt).write("Hello, world!")
```

---

### `write_bytes`
Writes the given bytes to the file at the specified path, creating the file if
it doesn't already exist. Sets the file permissions as specified. If the file
writing cannot be successfully completed, a runtime error is raised.

```tomo
func write(path: Path, bytes: [Byte], permissions=0o644[32] -> Void)
```

- `path`: The path of the file to write to.
- `bytes`: A list of bytes to write to the file.
- `permissions`: The permissions to set on the file if it is created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```tomo
(./file.txt).write_bytes([104[B], 105[B]])
```

---

### `write_unique`
Writes the given text to a unique file path based on the specified path. The
file is created if it doesn't exist. This is useful for creating temporary
files.

```tomo
func write_unique(path: Path, text: Text -> Path)
```

- `path`: The base path for generating the unique file. This path must include
  the string `XXXXXX` in the file base name.
- `text`: The text to write to the file.

**Returns:**  
The path of the newly created unique file.

**Example:**  
```tomo
>> created := (./file-XXXXXX.txt).write_unique("Hello, world!")
= (./file-27QHtq.txt)
>> created.read()
= "Hello, world!"
created.remove()
```

---

### `write_unique_bytes`
Writes the given bytes to a unique file path based on the specified path. The
file is created if it doesn't exist. This is useful for creating temporary
files.

```tomo
func write_unique_bytes(path: Path, bytes: [Byte] -> Path)
```

- `path`: The base path for generating the unique file. This path must include
  the string `XXXXXX` in the file base name.
- `bytes`: The bytes to write to the file.

**Returns:**  
The path of the newly created unique file.

**Example:**  
```tomo
>> created := (./file-XXXXXX.txt).write_unique_bytes([1[B], 2[B], 3[B]])
= (./file-27QHtq.txt)
>> created.read()
= [1[B], 2[B], 3[B]]
created.remove()
```
