% TOMO(1)
% Bruce Hill (*bruce@bruce-hill.com*)
% August 11, 2026

# NAME

tomo - The programming language of tomorrow.

# SYNOPSIS

`tomo` \[*global flags*\] \[*command*\] \[*command flags*\] \[*args...*\]

Run a program:
: `tomo` \[`run`\] *program.tm* \[`--` *args...*\]

Build an executable:
: `tomo` `build` \[`-o` *output*\] *program.tm*

Build a package:
: `tomo` `package` \[`-o` *libname.a*\] \[*dir-or-file...*\]

# DESCRIPTION

Tomo is a programming language that is statically typed, compiled, small, and
garbage-collected, with concise syntax and built-in support for
high-performance, low-overhead datastructures. It compiles by first outputting
C code, which is then compiled using the Zig toolchain bundled with the Tomo
installation.

With no command, `tomo` *file.tm* runs the file (like `tomo run`), and bare
`tomo` opens a scratch file in `$EDITOR` to edit and run (or compiles and runs
a program piped in on standard input).

# COMMANDS

`run` *file.tm* \[`--` *args...*\]
: Compile and run the given program. Anything after `--` is passed to the
program as its own arguments. The command name is optional: `tomo` *file.tm*
does the same thing.

`build` \[`-o` *output*\] *file.tm*
: Compile the given program to a standalone executable, placed as a sibling
of the `.tm` file (or at `-o` *output*).

`transpile` \[`--raw`\] *file.tm*
: Transpile the given file to C and print the generated header and source to
standard output, each preceded by a `// file:` line. The output is formatted
with `clang-format` and (when standard output is a terminal)
syntax-highlighted with `bat`, whichever of the two is installed; `--raw`
prints the raw generated code instead. The generated `.h`/`.c` files are also
written into the file's `.build/` directory.

`parse` *file.tm...*
: Print the parse tree of the given files as S-expressions.

`fmt` \[`-i`\] *file.tm...*
: Autoformat the given files and print them to standard output, or rewrite
them in place with `--in-place`/`-i`.

`package` \[`-o` *libname.a*\] \[*dir-or-file...*\]
: Build packages into static archives. With no arguments, the current
directory is built as a package: every `.tm` file not starting with an
underscore (or dot or digit) is compiled and archived into `package.a`.
Directory arguments are each built the same way; `.tm` file arguments are
compiled into a single archive. `-o` sets the archive path (single package
only).

`install` \[*dir-or-file...*\]
: Install packages and programs into the Tomo installation prefix. Directory
arguments are built as packages and installed into `lib/`; `.tm` file
arguments are compiled to executables and installed into `bin/` (with their
manpages). With no arguments, the current directory is installed as a package.

`uninstall` *name...*
: Uninstall the given packages or programs.

`uninstall-self` \[`-y`\]
: Uninstall this Tomo installation: remove this version's files from the
installation prefix, along with its per-user state, cross-compilation
target packs, and any bundled toolchains no remaining installation shares.
If other Tomo versions remain in the same prefix, the `tomo` and man page
symlinks are repointed to the newest one; otherwise they are removed too.
If no `tomo` remains anywhere on `$PATH` afterwards, the cache
(`~/.cache/tomo`: package downloads and the bundled Zig toolchain's compile
cache) is also cleared. Asks for confirmation unless `--yes`/`-y` is given.

`vendor` \[`-e`|`-u`\] *package...*
: Copy the named packages' digest-verified source archives into the current
project's `vendor/` directory and update `./packages.ini` to use the vendored
copies as the primary sources (keeping the digest pins and demoting the
previous sources to fallbacks). With `--editable`/`-e`, extract each package's
sources into `vendor/<name>/` and drop its digest pin, so the vendored copy
can be edited freely. With `--unvendor`/`-u`, undo vendoring: restore each
package's `./packages.ini` entry to its first non-vendored fallback source
(or the compiler's default pin), re-install the package into the project's
package store (re-pinning the digest if editable vendoring dropped it), and
delete the vendored copy.

`version`
: Print the compiler version and exit (with `--verbose`/`-v`: plus the git
revision).

`info` \[`-x`\] *binary...*
: Print the build info embedded in the given compiled files (executables or
package archives). With `--extract-source`/`-x`, extract the source files
embedded in each compiled program into a `<program>-source/` directory
instead. The extracted directory is a working project: it includes the
program's sources, `packages.ini`, license texts, and a pre-seeded package
store, so it can be rebuilt as-is without network access.

# GLOBAL OPTIONS

Global options are valid anywhere on the command line, before or after the
command name. Command-specific flags (like `build`'s `-o`) are only valid
after their command.

`--help`, `-h`
: Print the usage (or a command's usage) and exit.

`--verbose`, `-v`
: Print extra verbose output.

`--quiet`, `-q`
: Run in quiet mode.

`--optimization` **level**, `-O` **level**
: Set the optimization level.

`--force-rebuild`, `-f`
: Force rebuilding/recompiling.

`--source-mapping=`, `-m=` **<yes|no>**
: Toggle whether source mapping should be enabled or disabled.

`--target` *platform*
: Cross-compile for another platform instead of this machine's. The target
platform's libraries are installed on demand from its Tomo distribution
archive. Cross-compiled executables get the platform as a filename suffix
(e.g. `foo.aarch64-macos`) and cannot be run or installed on this machine.
Valid platforms are: `x86_64-linux`, `aarch64-linux`, `riscv64-linux`,
`powerpc64le-linux`, `s390x-linux`, `x86_64-macos`, `aarch64-macos`,
`x86_64-freebsd`, `aarch64-freebsd`, `x86_64-netbsd`, `aarch64-netbsd`,
`x86_64-openbsd`, and `aarch64-openbsd`.

`--install-target`
: When using `--target`, download and install the target platform's libraries
without asking for confirmation.
