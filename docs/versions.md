# Versioning

The Tomo language and Tomo libraries both use a versioning system based on a
changelog called `CHANGES.md` that includes a human-and-machine-readable
markdown list of versions.

The version number is parsed from the first level 2 header (i.e. line beginning
with `## `). An example CHANGES.md file might look like this:

```
# Version History

## v1.2

Version 1.2 adds some new features:

- Autofrobnication
- Reverse froodling

## v1.1

- Added a `--moop` compiler flag

## v1.0

Major version change including:

- Deprecated snobwozzling
- Added new syntax for sleazles

## v0.3

Bugfixes and new features...
```

When you build the compiler or a library, if this file exists, it will be used
to determine the current version (the top-most level 2 header).

## Tomo Language Versions

The version for the Tomo language itself will come into play in a few ways:

1. The compiler will be installed to `tomo_vX.Y` (where `X` is the major
   version number and `Y` is the minor version number).
2. A symbolic link will be installed from `tomo` to the largest version of Tomo
   that is installed on your machine (e.g. `~/.local/bin/tomo ->
   ~/.local/bin/tomo_v2.12`).
3. Each version of Tomo will build and install its own shared library file
   (e.g. `~/.local/lib/libtomo_v1.2.so`) and headers (e.g.
   `~/.local/include/tomo_v1.2/tomo.h`).
4. Tomo libraries will be installed to a separate subdirectory for each version
   of the compiler (e.g. `~/.local/share/tomo_v1.2/installed`).


# Rationale

I have opted to use a CHANGES.md file instead of git tags or a project
configuration file for a few reasons. The first is that I think there is real
value provided by maintaining a human-readable changelog. This approach
encourages developers to maintain one. The second is to facilitate a development
cycle where developers iterate on new features under the umbrella of a new
version number, rather than the git tag-based approach where new features are
developed in the no man's land between version tags. I also opted to use a
human-readable markdown changelog rather than a build configuration file in
order to ensure that there is no mismatch between the documentation and the
configuration. My recommendation would be to develop code on a `main` or `dev`
branch, bumping the version number pre-emptively (with an empty changelist). As
new changes come in, add them to the changelog. Then, when it's appropriate,
create a git tag to mark the current commit as the canonical one for that
semantic version. Then, make a new commit after the tagged one bumping the
version number and signaling the beginning of a new cycle of development.
