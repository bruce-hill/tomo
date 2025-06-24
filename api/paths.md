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
>> (./file.txt).accessed()
= 1704221100?
>> (./not-a-file).accessed()
= none

```
## Path.append

```tomo
Path.append : func(path: Path, text: Text, permissions: Int32 = Int32(0o644) -> Void)
```

Appends the given text to the file at the specified path, creating the file if it doesn't already exist. Failure to write will result in a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to append to.  | -
text | `Text` | The text to append to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is being created.  | `Int32(0o644)`

**Return:** Nothing.


**Example:**
```tomo
(./log.txt).append("extra line$(\n)")

```
## Path.append_bytes

```tomo
Path.append_bytes : func(path: Path, bytes: [Byte], permissions: Int32 = Int32(0o644) -> Void)
```

Appends the given bytes to the file at the specified path, creating the file if it doesn't already exist. Failure to write will result in a runtime error.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to append to.  | -
bytes | `[Byte]` | The bytes to append to the file.  | -
permissions | `Int32` | The permissions to set on the file if it is being created.  | `Int32(0o644)`

**Return:** Nothing.


**Example:**
```tomo
(./log.txt).append_bytes([104, 105])

```
## Path.base_name

```tomo
Path.base_name : func(path: Path -> Text)
```

Returns the base name of the file or directory at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** The base name of the file or directory.


**Example:**
```tomo
>> (./path/to/file.txt).base_name()
= "file.txt"

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
>> (/bin/sh).can_execute()
= yes
>> (/usr/include/stdlib.h).can_execute()
= no
>> (/non/existant/file).can_execute()
= no

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
>> (/usr/include/stdlib.h).can_read()
= yes
>> (/etc/shadow).can_read()
= no
>> (/non/existant/file).can_read()
= no

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
>> (/tmp).can_write()
= yes
>> (/etc/passwd).can_write()
= no
>> (/non/existant/file).can_write()
= no

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
>> (./file.txt).changed()
= 1704221100?
>> (./not-a-file).changed()
= none

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
>> (./directory).child("file.txt")
= (./directory/file.txt)

```
## Path.children

```tomo
Path.children : func(path: Path, include_hidden = no -> [Path])
```

Returns a list of children (files and directories) within the directory at the specified path. Optionally includes hidden files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `` | Whether to include hidden files, which start with a `.`.  | `no`

**Return:** A list of paths for the children.


**Example:**
```tomo
>> (./directory).children(include_hidden=yes)
= [".git", "foo.txt"]

```
## Path.create_directory

```tomo
Path.create_directory : func(path: Path, permissions = Int32(0o755) -> Void)
```

Creates a new directory at the specified path with the given permissions. If any of the parent directories do not exist, they will be created as needed.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory to create.  | -
permissions | `` | The permissions to set on the new directory.  | `Int32(0o755)`

**Return:** Nothing.


**Example:**
```tomo
(./new_directory).create_directory()

```
## Path.current_dir

```tomo
Path.current_dir : func(-> Path)
```

Creates a new directory at the specified path with the given permissions. If any of the parent directories do not exist, they will be created as needed.


**Return:** The absolute path of the current directory.


**Example:**
```tomo
>> Path.current_dir()
= (/home/user/tomo)

```
## Path.exists

```tomo
Path.exists : func(path: Path -> Bool)
```

Checks if a file or directory exists at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -

**Return:** `True` if the file or directory exists, `False` otherwise.


**Example:**
```tomo
>> (/).exists()
= yes

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
>> (~/foo).expand_home() # Assume current user is 'user'
= /home/user/foo
>> (/foo).expand_home() # No change
= /foo

```
## Path.extension

```tomo
Path.extension : func(path: Path, full: Bool = yes -> Text)
```

Returns the file extension of the file at the specified path. Optionally returns the full extension.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file.  | -
full | `Bool` | Whether to return everything after the first `.` in the base name, or only the last part of the extension.  | `yes`

**Return:** The file extension (not including the leading `.`) or an empty text if there is no file extension.


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
## Path.files

```tomo
Path.files : func(path: Path, include_hidden: Bool = no -> [Path])
```

Returns a list of files within the directory at the specified path. Optionally includes hidden files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `Bool` | Whether to include hidden files.  | `no`

**Return:** A list of file paths.


**Example:**
```tomo
>> (./directory).files(include_hidden=yes)
= [(./directory/file1.txt), (./directory/file2.txt)]

```
## Path.from_components

```tomo
Path.from_components : func(components: [Text] -> Path)
```

Returns a path built from a list of path components.

Argument | Type | Description | Default
---------|------|-------------|---------
components | `[Text]` | A list of path components.  | -

**Return:** A path representing the given components.


**Example:**
```tomo
>> Path.from_components(["/", "usr", "include"])
= /usr/include
>> Path.from_components(["foo.txt"])
= ./foo.txt
>> Path.from_components(["~", ".local"])
= ~/.local

```
## Path.glob

```tomo
Path.glob : func(path: Path -> [Path])
```

Perform a globbing operation and return a list of matching paths. Some glob specific details:
- The paths "." and ".." are *not* included in any globbing results.
- Files or directories that begin with "." will not match `*`, but will match `.*`.
- Globs do support `{a,b}` syntax for matching files that match any of several
  choices of patterns.

- The shell-style syntax `**` for matching subdirectories is not supported.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory which may contain special globbing characters like `*`, `?`, or `{...}`  | -

**Return:** A list of file paths that match the glob.


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
>> (/bin).group()
= "root"
>> (/non/existent/file).group()
= none

```
## Path.has_extension

```tomo
Path.has_extension : func(path: Path, extension: Text -> Bool)
```

Return whether or not a path has a given file extension.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
extension | `Text` | A file extension (leading `.` is optional). If empty, the check will test if the file does not have any file extension.  | -

**Return:** Whether or not the path has the given extension.


**Example:**
```tomo
>> (/foo.txt).has_extension("txt")
= yes
>> (/foo.txt).has_extension(".txt")
= yes
>> (/foo.tar.gz).has_extension("gz")
= yes
>> (/foo.tar.gz).has_extension("zip")
= no

```
## Path.is_directory

```tomo
Path.is_directory : func(path: Path, follow_symlinks = yes -> Bool)
```

Checks if the path represents a directory. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `` | Whether to follow symbolic links.  | `yes`

**Return:** `True` if the path is a directory, `False` otherwise.


**Example:**
```tomo
>> (./directory/).is_directory()
= yes

>> (./file.txt).is_directory()
= no

```
## Path.is_file

```tomo
Path.is_file : func(path: Path, follow_symlinks = yes -> Bool)
```

Checks if the path represents a file. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `` | Whether to follow symbolic links.  | `yes`

**Return:** `True` if the path is a file, `False` otherwise.


**Example:**
```tomo
>> (./file.txt).is_file()
= yes

>> (./directory/).is_file()
= no

```
## Path.is_socket

```tomo
Path.is_socket : func(path: Path, follow_symlinks = yes -> Bool)
```

Checks if the path represents a socket. Optionally follows symbolic links.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -
follow_symlinks | `` | Whether to follow symbolic links.  | `yes`

**Return:** `True` if the path is a socket, `False` otherwise.


**Example:**
```tomo
>> (./socket).is_socket()
= yes

```
## Path.is_symlink

```tomo
Path.is_symlink : func(path: Path -> Bool)
```

Checks if the path represents a symbolic link.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to check.  | -

**Return:** `True` if the path is a symbolic link, `False` otherwise.


**Example:**
```tomo
>> (./link).is_symlink()
= yes

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
>> (./file.txt).modified()
= 1704221100?
>> (./not-a-file).modified()
= none

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
>> (/bin).owner()
= "root"
>> (/non/existent/file).owner()
= none

```
## Path.parent

```tomo
Path.parent : func(path: Path -> Path)
```

Returns the parent directory of the file or directory at the specified path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file or directory.  | -

**Return:** The path of the parent directory.


**Example:**
```tomo
>> (./path/to/file.txt).parent()
= (./path/to/)

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
>> (./hello.txt).read()
= "Hello"?

>> (./nosuchfile.xxx).read()
= none

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
>> (./hello.txt).read()
= [72, 101, 108, 108, 111]?

>> (./nosuchfile.xxx).read()
= none

```
## Path.relative_to

```tomo
Path.relative_to : func(path: Path, relative_to = (./) -> Path)
```

Returns the path relative to a given base path. By default, the base path is the current directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to convert.  | -
relative_to | `` | The base path for the relative path.  | `(./)`

**Return:** The relative path.


**Example:**
```tomo
>> (./path/to/file.txt).relative(relative_to=(./path))
= (./to/file.txt)

```
## Path.remove

```tomo
Path.remove : func(path: Path, ignore_missing = no -> Void)
```

Removes the file or directory at the specified path. A runtime error is raised if something goes wrong.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to remove.  | -
ignore_missing | `` | Whether to ignore errors if the file or directory does not exist.  | `no`

**Return:** Nothing.


**Example:**
```tomo
(./file.txt).remove()

```
## Path.resolved

```tomo
Path.resolved : func(path: Path, relative_to = (./) -> Path)
```

Resolves the absolute path of the given path relative to a base path. By default, the base path is the current directory.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to resolve.  | -
relative_to | `` | The base path for resolution.  | `(./)`

**Return:** The resolved absolute path.


**Example:**
```tomo
>> (~/foo).resolved()
= (/home/user/foo)

>> (./path/to/file.txt).resolved(relative_to=(/foo))
= (/foo/path/to/file.txt)

```
## Path.set_owner

```tomo
Path.set_owner : func(path: Path, owner: Text? = none, group: Text? = none, follow_symlinks: Bool = yes -> Void)
```

Set the owning user and/or group for a path.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path to change the permissions for.  | -
owner | `Text?` | If non-none, the new user to assign to be the owner of the file.  | `none`
group | `Text?` | If non-none, the new group to assign to be the owner of the file.  | `none`
follow_symlinks | `Bool` | Whether to follow symbolic links.  | `yes`

**Return:** Nothing. If a path does not exist, a failure will be raised.


**Example:**
```tomo
(./file.txt).set_owner(owner="root", group="wheel")

```
## Path.sibling

```tomo
Path.sibling : func(path: Path, name: Text -> Path)
```

Return a path that is a sibling of another path (i.e. has the same parent, but a different name). This is equivalent to `.parent().child(name)`

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | A path.  | -
name | `Text` | The name of a sibling file or directory.  | -

**Return:** A new path representing the sibling.


**Example:**
```tomo
>> (/foo/baz).sibling("doop")
= (/foo/doop)

```
## Path.subdirectories

```tomo
Path.subdirectories : func(path: Path, include_hidden = no -> [Path])
```

Returns a list of subdirectories within the directory at the specified path. Optionally includes hidden subdirectories.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the directory.  | -
include_hidden | `` | Whether to include hidden subdirectories.  | `no`

**Return:** A list of subdirectory paths.


**Example:**
```tomo
>> (./directory).subdirectories()
= [(./directory/subdir1), (./directory/subdir2)]

>> (./directory).subdirectories(include_hidden=yes)
= [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]

```
## Path.unique_directory

```tomo
Path.unique_directory : func(path: Path -> Path)
```

Generates a unique directory path based on the given path. Useful for creating temporary directories.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique directory. The last six letters of this path must be `XXXXXX`.  | -

**Return:** A unique directory path after creating the directory.


**Example:**
```tomo
>> created := (/tmp/my-dir.XXXXXX).unique_directory()
= (/tmp/my-dir-AwoxbM/)
>> created.is_directory()
= yes
created.remove()

```
## Path.write

```tomo
Path.write : func(path: Path, text: Text, permissions = Int32(0o644) -> Void)
```

Writes the given text to the file at the specified path, creating the file if it doesn't already exist. Sets the file permissions as specified. If the file writing cannot be successfully completed, a runtime error is raised.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
text | `Text` | The text to write to the file.  | -
permissions | `` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Nothing.


**Example:**
```tomo
(./file.txt).write("Hello, world!")

```
## Path.write_bytes

```tomo
Path.write_bytes : func(path: Path, bytes: [Byte], permissions = Int32(0o644) -> Void)
```

Writes the given bytes to the file at the specified path, creating the file if it doesn't already exist. Sets the file permissions as specified. If the file writing cannot be successfully completed, a runtime error is raised.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The path of the file to write to.  | -
bytes | `[Byte]` | A list of bytes to write to the file.  | -
permissions | `` | The permissions to set on the file if it is created.  | `Int32(0o644)`

**Return:** Nothing.


**Example:**
```tomo
(./file.txt).write_bytes([104, 105])

```
## Path.write_unique

```tomo
Path.write_unique : func(path: Path, text: Text -> Path)
```

Writes the given text to a unique file path based on the specified path. The file is created if it doesn't exist. This is useful for creating temporary files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique file. This path must include the string `XXXXXX` in the file base name.  | -
text | `Text` | The text to write to the file.  | -

**Return:** The path of the newly created unique file.


**Example:**
```tomo
>> created := (./file-XXXXXX.txt).write_unique("Hello, world!")
= (./file-27QHtq.txt)
>> created.read()
= "Hello, world!"
created.remove()

```
## Path.write_unique_bytes

```tomo
Path.write_unique_bytes : func(path: Path, bytes: [Byte] -> Path)
```

Writes the given bytes to a unique file path based on the specified path. The file is created if it doesn't exist. This is useful for creating temporary files.

Argument | Type | Description | Default
---------|------|-------------|---------
path | `Path` | The base path for generating the unique file. This path must include the string `XXXXXX` in the file base name.  | -
bytes | `[Byte]` | The bytes to write to the file.  | -

**Return:** The path of the newly created unique file.


**Example:**
```tomo
>> created := (./file-XXXXXX.txt).write_unique_bytes([1, 2, 3])
= (./file-27QHtq.txt)
>> created.read()
= [1, 2, 3]
created.remove()

```
