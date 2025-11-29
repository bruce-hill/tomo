# Version History

## v2025-11-29.2

### Bugfixes

- Fix for undefined behavior on enums and structs with padding.

## v2025-11-29

### Syntax changes

- Syntax for tables has changed to use colons (`{k: v}`) instead of equals
  (`{k=v}`).
- Syntax for text literals and inline C code has been simplified.
- Added metadata format instead of `_HELP`/`_USAGE`:
  ```
  HELP: "Help text"
  USAGE: "Usage text"
  MANPAGE_SYNOPSYS: "Synopsys..."
  MANPAGE_DESCRIPTION: (./description.txt)
  ```
- **Deprecated:** `extern` keyword for declaring external symbols from C.
  - Use `C_code` instead.
- **Deprecated:** postfix `?` to make values optional.
  - Explicitly optional values can be declared as `my_var : T? = value`.
- **Deprecated:** `>> ... = ...` form of doctests. They are now called "debug logs"
  and you can specify multiple values: `>> a, b, c`
- **Deprecated:** `extend` blocks
- **Deprecated:** `deserialize` operation and `.serialized()` method call
  - Instead, convert to and from `[Byte]`

### Versioning and library changes

- Tomo versioning now uses dates instead of semantic versioning.
- Tomo libraries are now installed to
  `$TOMO_PATH/lib/tomo@TOMO_VERSION/library@LIBRARY_VERSION` instead of
  `$TOMO_PATH/share/tomo_TOMO_VERSION/installed/module_LIBRARY_VERSION`
- Core libraries are no longer shipped with the compiler, they have moved to
  separate repositories.
- Library installation has been cleaned up a bit.

### Type Changes

- List indexing now gives an optional value.
- Added support for inline anonymous enums
- Accessing a field on an enum now gives an optional value instead of a boolean.
- **Deprecated**: Sets are no longer a separate type with separate methods.
  - Instead of sets, use tables with a value type of `{KeyType:Empty}`.
  - As a shorthand, you can use `{a,b,c}` instead of `{a:Empty(),
    b:Empty(), c:Empty()}` and the type annotation `{K}` as shorthand for
    `{K:Empty}`.
- Added `Empty` for a built-in empty struct type and `EMPTY` for an instance of
  the empty struct.
- Struct fields that start with underscores can be accessed again and function
  arguments that start with underscore can be passed (but only as keyword
  arguments).

### API changes

- Added `Path.lines()`.
- Added `Text.find(text, target, start=1)`.
- Added `at_cleanup()` to register cleanup functions.
- Added `recursive` argument to `Path.create_directory()` to create parent
  directories if needed.
- `setenv()` now takes an optional parameter for value, which allows for
  unsetting environment values.
- Tables now have `and`, `or`, `xor`, and `-` (minus) metamethods.
- Added `table.with(other)`, `table.without(other)`,
  `table.intersection(other)`, and `table.difference(other)`.
- Changed `list.unique()` to return a table with `Empty()` values for each
  unique list item.
- Added a `--format` flag to the `tomo` binary that autoformats your code
  (currently unstable, do not rely on it just yet).
- Standardized text methods for Unicode encodings:
  - `Text.from_utf8()`/`Text.utf8()`
  - `Text.from_utf16()`/`Text.utf16()`
  - `Text.from_utf32()`/`Text.utf32()`

### Bug fixes

- `Int.parse()` had a memory bug.
- Breaking out of a `for line in file.by_line()!` loop would leak file handle
  resources, which could lead to exhausting the number of open file handles.
  When that happens, the standard library now forces a GC collection to clean
  up resources, which can result in file handles being freed up.
- `&` references failed to propagate when accessing fields like
  `foo.baz.method()` when `foo` is a `&Foo` and `baz.method()` takes a `&Baz`.
- Optional paths no longer fail to compile when you check them for `none`.
- Text replacement no longer infinitely loops when given an empty text to replace.
- Short CLI flag aliases now no longer use the first letter of the argument.
- Stack memory was not correctly detected in some cases, leading to potential
  memory errors.

### Other changes

- Added automatic manpage generation.
- Major improvements to robustness of CLI argument parsing.

## v0.3

- Added a versioning system based on `CHANGES.md` files and `modules.ini`
  configuration for module aliases.
- When attempting to run a program with a module that is not installed, Tomo
  can prompt the user to automatically install it.
- Programs can use `--version` as a CLI flag to print a Tomo program's version
  number and exit.
- Significant improvements to type inference to allow more expressions to be
  compiled into known types in a less verbose manner. For example:
  ```tomo
  enum NumberOrText(Number(n:Num), SomeText(text:Text))
  func needs_number_or_text(n:NumberOrText)
      >> n
  func main()
      needs_number_or_text(123)
      needs_number_or_text(123.5)
      needs_number_or_text("Hello")
  ```
- Added `tomo --prefix` to print the Tomo install prefix.
- Sets now support infix operations for `and`, `or`, `xor`, and `-`.
- Added new `json` module for JSON parsing and encoding.
- Added `Path.sibling()`.
- Added `Path.has_extension()`.
- Added `Table.with_fallback()`.
- Added `Int*.get_bit()` and `Byte.get_bit()`.
- Added `Byte.parse()` to parse bytes from text.
- Added optional `remainder` parameter to `parse()` methods, which (if
  non-none) receives the remaining text after the match. If `none`, the match
  will fail unless it consumes the whole text.
- Added optional `remainder` parameter to `Text.starts_with()` and
  `Text.ends_with()` to allow you to get the rest of the text without two
  function calls.
- Improved space efficiency of Text that contains non-ASCII codepoints.
- Doctests now use equality checking instead of converting to text.
- Fixed the following bugs:
  - Negative integers weren't converting to text properly.
  - Mutation of a collection during iteration was violating value semantics.
  - `extend` statements weren't properly checking that the type name was valid.
  - Lazy recompilation wasn't happening when `use ./foo.c` was used for local
    C/assembly files or their `#include`s.
  - Memory offsets for enums with different member alignments were miscalculated.
  - Optional types with trailing padding were not correctly being detected as `none`
  - Tomo identifiers that happened to coincide with C keywords were not allowed.
  - Compatibility issues caused compilation failure on some platforms.

## v0.2

- Improved compatibility on different platforms.
- Switched to use a per-file unique ID suffix instead of renaming symbols after
  compilation with `objcopy`.
- Installation process now sets user permissions as needed, which fixes an
  issue where a user not in the sudoers file couldn't install even to a local
  directory.
- Fixed some bugs with Table and Text hashing.
- Various other bugfixes and internal optimizations.

## v0.1

First version to get a version number.
