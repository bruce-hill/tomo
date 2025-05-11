# Version History

## v0.3

- Added a versioning system based on CHANGES.md files.

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
