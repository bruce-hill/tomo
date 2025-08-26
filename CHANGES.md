# Version History

## v0.3

- Added a versioning system based on `CHANGES.md` files and `modules.ini`
  configuration for module aliases.
- Added a `--format` flag to the `tomo` binary that autoformats your code.
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
