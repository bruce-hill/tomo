# Version History

## v0.3

- Added `Dec` type for decimal floating point numbers (literal `$12.34`)
- Added a versioning system based on `CHANGES.md` files and `modules.ini`
  configuration for module aliases.
- When attempting to run a program with a module that is not installed, Tomo
  can prompt the user to automatically install it.
- Programs can use `--version` as a CLI flag to print a Tomo program's version
  number and exit.
- Added `tomo --prefix` to print the Tomo install prefix.
- Sets now support infix operations for `and`, `or`, `xor`, and `-`
- Added Path.sibling()
- Added Path.has_extension()
- Added Table.with_fallback()
- Added Int*.get_bit() and Byte.get_bit()
- Fixed bugs:
  - Negative integers weren't converting to text properly.
  - Mutation of a collection during iteration was violating value semantics.
  - `extend` statements weren't properly checking that the type name was valid.
  - Lazy recompilation wasn't happening when `use ./foo.c` was used for local
    C/assembly files or their `#include`s.

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
