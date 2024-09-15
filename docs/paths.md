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
`Path.from_unsafe_text(text)` if you need to use arbitrary text as a file path.

## Path Methods

### `append`

**Description:**  
Appends the given text to the file at the specified path, creating the file if
it doesn't already exist. Failure to write will result in a runtime error.

**Usage:**  
```markdown
append(path: Path, text: Text, permissions: Int32 = 0o644[32]) -> Void
```

**Parameters:**

- `path`: The path of the file to append to.
- `text`: The text to append to the file.
- `permissions` (optional): The permissions to set on the file if it is being created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```markdown
(./log.txt):append("extra line$(\n)")
```

---

### `append_bytes`

**Description:**  
Appends the given bytes to the file at the specified path, creating the file if
it doesn't already exist. Failure to write will result in a runtime error.

**Usage:**  
```markdown
append_bytes(path: Path, bytes: [Byte], permissions: Int32 = 0o644[32]) -> Void
```

**Parameters:**

- `path`: The path of the file to append to.
- `bytes`: The bytes to append to the file.
- `permissions` (optional): The permissions to set on the file if it is being created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```markdown
(./log.txt):append_bytes([104[B], 105[B]])
```

---

### `base_name`

**Description:**  
Returns the base name of the file or directory at the specified path.

**Usage:**  
```markdown
base_name(path: Path) -> Text
```

**Parameters:**

- `path`: The path of the file or directory.

**Returns:**  
The base name of the file or directory.

**Example:**  
```markdown
>> (./path/to/file.txt):base_name()
= "file.txt"
```

---

### `by_line`

**Description:**  
Returns an iterator that can be used to iterate over a file one line at a time,
or returns a null value if the file could not be opened.

**Usage:**  
```markdown
by_line(path: Path) -> (func()->Text?)?
```

**Parameters:**

- `path`: The path of the file.

**Returns:**  
An iterator that can be used to get lines from a file one at a time or a null
value if the file couldn't be read.

**Example:**  
```markdown
# Safely handle file not being readable:
if lines := (./file.txt):by_line():
    for line in lines:
        say(line:upper())
else:
    say("Couldn't read file!")

# Assume the file is readable and error if that's not the case:
for line in (/dev/stdin):by_line()!:
    say(line:upper())
```

---

### `children`

**Description:**  
Returns a list of children (files and directories) within the directory at the specified path. Optionally includes hidden files.

**Usage:**  
```markdown
children(path: Path, include_hidden=no) -> [Path]
```

**Parameters:**

- `path`: The path of the directory.
- `include_hidden` (optional): Whether to include hidden files, which start with a `.` (default is `no`).

**Returns:**  
A list of paths for the children.

**Example:**  
```markdown
>> (./directory):children(include_hidden=yes)
= [".git", "foo.txt"]
```

---

### `create_directory`

**Description:**  
Creates a new directory at the specified path with the given permissions.

**Usage:**  
```markdown
create_directory(path: Path, permissions=0o644[32]) -> Void
```

**Parameters:**

- `path`: The path of the directory to create.
- `permissions` (optional): The permissions to set on the new directory (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```markdown
(./new_directory):create_directory()
```

---

### `exists`

**Description:**  
Checks if a file or directory exists at the specified path.

**Usage:**  
```markdown
exists(path: Path) -> Bool
```

**Parameters:**

- `path`: The path to check.

**Returns:**  
`True` if the file or directory exists, `False` otherwise.

**Example:**  
```markdown
>> (/):exists()
= yes
```

---

### `extension`

**Description:**  
Returns the file extension of the file at the specified path. Optionally returns the full extension.

**Usage:**  
```markdown
extension(path: Path, full=yes) -> Text
```

**Parameters:**

- `path`: The path of the file.
- `full` (optional): Whether to return everything after the first `.` in the
  base name, or only the last part of the extension (default is `yes`).

**Returns:**  
The file extension (not including the leading `.`) or an empty text if there is
no file extension.

**Example:**  
```markdown
>> (./file.tar.gz):extension()
= "tar.gz"
>> (./file.tar.gz):extension(full=no)
= "gz"
>> (/foo):extension()
= ""
>> (./.git):extension()
= ""
```

---

### `files`

**Description:**  
Returns a list of files within the directory at the specified path. Optionally includes hidden files.

**Usage:**  
```markdown
files(path: Path, include_hidden=no) -> [Path]
```

**Parameters:**

- `path`: The path of the directory.
- `include_hidden` (optional): Whether to include hidden files (default is `no`).

**Returns:**  
A list of file paths.

**Example:**  
```markdown
>> (./directory):files(include_hidden=yes)
= [(./directory/file1.txt), (./directory/file2.txt)]
```

---

### `is_directory`

**Description:**  
Checks if the path represents a directory. Optionally follows symbolic links.

**Usage:**  
```markdown
is_directory(path: Path, follow_symlinks=yes) -> Bool
```

**Parameters:**

- `path`: The path to check.
- `follow_symlinks` (optional): Whether to follow symbolic links (default is `yes`).

**Returns:**  
`True` if the path is a directory, `False` otherwise.

**Example:**  
```markdown
>> (./directory/):is_directory()
= yes

>> (./file.txt):is_directory()
= no
```

---

### `is_file`

**Description:**  
Checks if the path represents a file. Optionally follows symbolic links.

**Usage:**  
```markdown
is_file(path: Path, follow_symlinks=yes) -> Bool
```

**Parameters:**

- `path`: The path to check.
- `follow_symlinks` (optional): Whether to follow symbolic links (default is `yes`).

**Returns:**  
`True` if the path is a file, `False` otherwise.

**Example:**  
```markdown
>> (./file.txt):is_file()
= yes

>> (./directory/):is_file()
= no
```

---

### `is_socket`

**Description:**  
Checks if the path represents a socket. Optionally follows symbolic links.

**Usage:**  
```markdown
is_socket(path: Path, follow_symlinks=yes) -> Bool
```

**Parameters:**

- `path`: The path to check.
- `follow_symlinks` (optional): Whether to follow symbolic links (default is `yes`).

**Returns:**  
`True` if the path is a socket, `False` otherwise.

**Example:**  
```markdown
>> (./socket):is_socket()
= yes
```

---

### `is_symlink`

**Description:**  
Checks if the path represents a symbolic link.

**Usage:**  
```markdown
is_symlink(path: Path) -> Bool
```

**Parameters:**

- `path`: The path to check.

**Returns:**  
`True` if the path is a symbolic link, `False` otherwise.

**Example:**  
```markdown
>> (./link):is_symlink()
= yes
```

---

### `parent`

**Description:**  
Returns the parent directory of the file or directory at the specified path.

**Usage:**  
```markdown
parent(path: Path) -> Path
```

**Parameters:**

- `path`: The path of the file or directory.

**Returns:**  
The path of the parent directory.

**Example:**  
```markdown
>> (./path/to/file.txt):parent()
= (./path/to/)
```

---

### `read`

**Description:**  
Reads the contents of the file at the specified path or a null value if the
file could not be read.

**Usage:**  
```markdown
read(path: Path) -> Text?
```

**Parameters:**

- `path`: The path of the file to read.

**Returns:**  
The contents of the file. If the file could not be read, a null value will be
returned. If the file can be read, but is not valid UTF8 data, an error will be
raised.

**Example:**  
```markdown
>> (./hello.txt):read()
= "Hello"?

>> (./nosuchfile.xxx):read()
= !Text
```
---

### `read_bytes`

**Description:**  
Reads the contents of the file at the specified path or a null value if the
file could not be read.

**Usage:**  
```markdown
read_bytes(path: Path) -> [Byte]?
```

**Parameters:**

- `path`: The path of the file to read.

**Returns:**  
The byte contents of the file. If the file cannot be read, a null value will be
returned.

**Example:**  
```markdown
>> (./hello.txt):read()
= [72[B], 101[B], 108[B], 108[B], 111[B]]?

>> (./nosuchfile.xxx):read()
= ![Byte]
```

---

### `relative`

**Description:**  
Returns the path relative to a given base path. By default, the base path is the current directory.

**Usage:**  
```markdown
relative(path: Path, relative_to=(./)) -> Path
```

**Parameters:**

- `path`: The path to convert.
- `relative_to` (optional): The base path for the relative path (default is `./`).

**Returns:**  
The relative path.

**Example:**  
```markdown
>> (./path/to/file.txt):relative(relative_to=(./path))
= (./to/file.txt)
```

---

### `remove`

**Description:**  
Removes the file or directory at the specified path. A runtime error is raised if something goes wrong.

**Usage:**  
```markdown
remove(path: Path, ignore_missing=no) -> Void
```

**Parameters:**

- `path`: The path to remove.
- `ignore_missing` (optional): Whether to ignore errors if the file or directory does not exist (default is `no`).

**Returns:**  
Nothing.

**Example:**  
```markdown
(./file.txt):remove()
```

---

### `resolved`

**Description:**  
Resolves the absolute path of the given path relative to a base path. By default, the base path is the current directory.

**Usage:**  
```markdown
resolved(path: Path, relative_to=(./)) -> Path
```

**Parameters:**

- `path`: The path to resolve.
- `relative_to` (optional): The base path for resolution (default is `./`).

**Returns:**  
The resolved absolute path.

**Example:**  
```markdown
>> (~/foo):resolved()
= (/home/user/foo)

>> (./path/to/file.txt):resolved(relative_to=(/foo))
= (/foo/path/to/file.txt)
```

---

### `subdirectories`

**Description:**  
Returns a list of subdirectories within the directory at the specified path. Optionally includes hidden subdirectories.

**Usage:**  
```markdown
subdirectories(path: Path, include_hidden=no) -> [Path]
```

**Parameters:**

- `path`: The path of the directory.
- `include_hidden` (optional): Whether to include hidden subdirectories (default is `no`).

**Returns:**  
A list of subdirectory paths.

**Example:**  
```markdown
>> (./directory):subdirectories()
= [(./directory/subdir1), (./directory/subdir2)]

>> (./directory):subdirectories(include_hidden=yes)
= [(./directory/.git), (./directory/subdir1), (./directory/subdir2)]
```

---

### `unique_directory`

**Description:**  
Generates a unique directory path based on the given path. Useful for creating temporary directories.

**Usage:**  
```markdown
unique_directory(path: Path) -> Path
```

**Parameters:**

- `path`: The base path for generating the unique directory. The last six letters of this path must be `XXXXXX`.

**Returns:**  
A unique directory path after creating the directory.

**Example:**  

```
>> created := (/tmp/my-dir.XXXXXX):unique_directory()
= (/tmp/my-dir-AwoxbM/)
>> created:is_directory()
= yes
created:remove()
```

---

### `write`

**Description:**  
Writes the given text to the file at the specified path, creating the file if
it doesn't already exist. Sets the file permissions as specified. If the file
writing cannot be successfully completed, a runtime error is raised.

**Usage:**  
```markdown
write(path: Path, text: Text, permissions=0o644[32]) -> Void
```

**Parameters:**

- `path`: The path of the file to write to.
- `text`: The text to write to the file.
- `permissions` (optional): The permissions to set on the file if it is created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```markdown
(./file.txt):write("Hello, world!")
```

---

### `write_bytes`

**Description:**  
Writes the given bytes to the file at the specified path, creating the file if
it doesn't already exist. Sets the file permissions as specified. If the file
writing cannot be successfully completed, a runtime error is raised.

**Usage:**  
```markdown
write(path: Path, bytes: [Byte], permissions=0o644[32]) -> Void
```

**Parameters:**

- `path`: The path of the file to write to.
- `bytes`: An array of bytes to write to the file.
- `permissions` (optional): The permissions to set on the file if it is created (default is `0o644`).

**Returns:**  
Nothing.

**Example:**  
```markdown
(./file.txt):write_bytes([104[B], 105[B]])
```

---

### `write_unique`

**Description:**  
Writes the given text to a unique file path based on the specified path. The
file is created if it doesn't exist. This is useful for creating temporary
files.

**Usage:**  
```markdown
write_unique(path: Path, text: Text) -> Path
```

**Parameters:**

- `path`: The base path for generating the unique file. This path must include
  the string `XXXXXX` in the file base name.
- `text`: The text to write to the file.

**Returns:**  
The path of the newly created unique file.

**Example:**  
```markdown
>> created := (./file-XXXXXX.txt):write_unique("Hello, world!")
= (./file-27QHtq.txt)
>> created:read()
= "Hello, world!"
created:remove()
```

---

### `write_unique_bytes`

**Description:**  
Writes the given bytes to a unique file path based on the specified path. The
file is created if it doesn't exist. This is useful for creating temporary
files.

**Usage:**  
```markdown
write_unique_bytes(path: Path, bytes: [Byte]) -> Path
```

**Parameters:**

- `path`: The base path for generating the unique file. This path must include
  the string `XXXXXX` in the file base name.
- `bytes`: The bytes to write to the file.

**Returns:**  
The path of the newly created unique file.

**Example:**  
```markdown
>> created := (./file-XXXXXX.txt):write_unique_bytes([1[B], 2[B], 3[B]])
= (./file-27QHtq.txt)
>> created:read()
= [1[B], 2[B], 3[B]]
created:remove()
```
