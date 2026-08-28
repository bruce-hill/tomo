% TOMO(1)
% Bruce Hill (*bruce@bruce-hill.com*)
% August 16, 2026

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

A Tomo installation is relocatable: the `tomo` binary lives at
*prefix*`/bin/tomo@`*version*, and it locates its libraries, headers, bundled
toolchain, and man pages at startup relative to its own path (the *prefix* is
the grandparent directory of the binary), so the whole tree can be moved or
extracted into any prefix without rebuilding. The prefix is not fixed at build
time; set `$TOMO_PATH` to override the automatically determined location (see
**ENVIRONMENT**).

With no command, `tomo` *file.tm* runs the file (like `tomo run`), and bare
`tomo` opens a scratch file in `$EDITOR` to edit and run (or compiles and runs
a program piped in on standard input).

# COMMANDS

Every command takes a `--verbose`/`-v` flag that turns on all build logs (the
compile steps, up-to-date "Unchanged" notices, and the toolchain commands being
run). Commands that print build progress by default (`build`, `package`,
`install`) also take `--quiet`/`-q` to silence it. The run-style commands
(`tomo` *file.tm*, `run`, `eval`) are silent by default: use `tomo run -v`
*file.tm* to see the compiler's work.

`run` \[`--instrument`\] *file.tm* \[`--` *args...*\]
: Compile and run the given program. Anything after `--` is passed to the
program as its own arguments. The command name is optional: `tomo` *file.tm*
does the same thing. With `--instrument`, the program is compiled with
profiling instrumentation and prints a breakdown of where its time went when
it exits (see **PROFILING**).

`eval` *'expr'*
: Evaluate a Tomo expression and print its result. The argument may be several
statements separated by newlines or `;` (for example `tomo eval 'use random;
random.int(1, 100)'`); the value of the final statement is printed, with syntax
coloring when standard output is a terminal.

`build` \[`-o` *output*\] \[`--install`\] \[`--prefix` *dir*\] \[`-y`\]
\[`--instrument`\] *file.tm*
: Compile the given program to a standalone executable, placed as a sibling
of the `.tm` file (or at `-o` *output*). With `--instrument`, the executable
is compiled with profiling instrumentation and prints a breakdown of where its
time went when it exits (see **PROFILING**). With `--install`, the executable and
its generated manpage are also copied into a prefix's `bin/` and `man/man1/` —
the installation prefix by default, or `--prefix` *dir* to choose another.
Existing files at those destinations are overwritten only after confirmation,
or immediately with `--yes`/`-y`; a warning is printed if the target `bin/` is
not on your `$PATH`. Remove installed programs again with `tomo uninstall`
*name*.

`transpile` \[`--raw`\] \[`--instrument`\] *file.tm*
: Transpile the given file to C and print the generated header and source to
standard output, each preceded by a `// file:` line. The output is formatted
with `clang-format` and (when standard output is a terminal)
syntax-highlighted with `bat`, whichever of the two is installed; `--raw`
prints the raw generated code instead. The generated `.h`/`.c` files are also
written into the file's `.tomo/` directory.

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

`uninstall` \[`-y`\] \[*name-or-path...*\]
: With one or more arguments, uninstall those installed programs. Each
argument is either a bare *name* (looked up in the prefix's `bin/`) or a
path to a binary. A binary is removed only if it is a Tomo program (it
carries Tomo's embedded build info), and its Tomo-generated manpage in
`man/man1/` is removed along with it. Files Tomo did not create are left
untouched, and a missing binary or manpage is only a warning. Install
programs with `tomo build --install`.

: With no arguments, uninstall this whole Tomo installation: remove this
version's files from the installation prefix, along with its per-user state,
cross-compilation target packs, and any bundled toolchains no remaining
installation shares. If other Tomo versions remain in the same prefix, the
`tomo` and man page symlinks are repointed to the newest one; otherwise they
are removed too. If no `tomo` remains anywhere on `$PATH` afterwards, the
cache (`~/.cache/tomo`: package downloads and the bundled Zig toolchain's
compile cache) is also cleared. Asks for confirmation unless `--yes`/`-y` is
given.

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

# PROFILING

A program compiled with `--instrument` (`tomo build --instrument` *file.tm*,
or `tomo run --instrument` *file.tm*) times every function, conversion, and
lambda it defines, and prints a report to standard error when it exits: for
each function that ran, the number of calls, the time spent inside it (with
its callees' time subtracted), the time spent in it and everything it called,
and the per-call average, sorted by the first of those. The report is printed
even when the program exits early, fails, or crashes. Instrumented calls cost
around 13ns each (two reads of the CPU's cycle counter), charged to the calling
function, so `--instrument` is for finding where the time goes rather than for
measuring absolute speed.

Setting `FLAME_GRAPH` to a path additionally writes the call tree there as an
SVG flame graph: one box per frame, as wide as its share of the run and stacked
on the frame that called it, each carrying its details in a hover tooltip.

The program's own arguments are left alone; the report is controlled by
`PROFILE`, `PROFILE_FILE`, and `FLAME_GRAPH` (see **ENVIRONMENT**). See also
*docs/profiling.md*.

# ENVIRONMENT

`TOMO_PATH`
: The installation prefix Tomo compiles and links against (its `lib/`,
`include/`, `libexec/`, and `man/` trees) and installs into. By default this
is determined at runtime from the location of the running `tomo` binary — the
grandparent of the resolved executable path — so an installation is
relocatable. Set `TOMO_PATH` to override it, for example to point at a tree in
an unusual location or one whose `tomo` binary has been copied away from its
sibling directories.

`TOMO_DIST_URL`
: Base URL for downloading distribution archives (used by `--target` and
`--install-target` to fetch a target platform's libraries). Defaults to
*https://tomo.bruce-hill.com/dist*.

`EDITOR`
: The editor bare `tomo` opens the scratch file in. Defaults to `vim`.

`NO_COLOR`, `COLOR`
: Set `NO_COLOR` to any non-empty value to disable colored output. Set `COLOR`
to `1` to force it on; otherwise color is used only when standard output is a
terminal.

`TOMO_STACKTRACE`, `TOMO_PLAIN_ERRORS`, `TOMO_CORE_DUMP`
: Diagnostics for compiler and runtime errors: `TOMO_STACKTRACE` prints a
stack trace on errors, `TOMO_PLAIN_ERRORS` disables the fancy source-quoting
error formatting, and setting `TOMO_CORE_DUMP` to a truthy value makes a fatal
error abort (dumping core) instead of exiting cleanly.

`TOMO_TEST_FILTER`, `TOMO_TEST_TIMEOUT`, `TOMO_TEST_VERBOSE`
: Control `tomo test` runs: only run tests whose label contains
`TOMO_TEST_FILTER`, cap each test at `TOMO_TEST_TIMEOUT` seconds, and print
verbose output when `TOMO_TEST_VERBOSE` is set.

`PROFILE`, `PROFILE_FILE`, `FLAME_GRAPH`
: Control the profile report of a program compiled with `--instrument` (see
**PROFILING**). Setting `PROFILE` to `0` (or `no`/`false`) makes the program
collect nothing and print nothing, so an instrumented binary can also be run
as a normal one. `PROFILE_FILE` writes the table to that path instead of
standard error (`-` means standard output). `FLAME_GRAPH` writes an SVG flame
graph to that path as well as the table. All three are read by the compiled
program, not by `tomo`.

`ZIG_GLOBAL_CACHE_DIR`
: Where the bundled Zig toolchain keeps its global compile cache. If unset,
Tomo points it at its own cache directory (under `$XDG_CACHE_HOME/tomo`) rather
than Zig's default, so it never mingles with a separately-run Zig's cache; an
explicit value is respected.

# FILES

*prefix*`/bin/tomo@`*version*
: The compiler binary, with a `bin/tomo` symlink to the newest coresident
version. Programs installed with `tomo build --install` also land in
*prefix*`/bin/`.

*prefix*`/lib/tomo@`*version*`/`, *prefix*`/include/tomo@`*version*`/`
: The Tomo standard library archive and headers this version links against.

*prefix*`/man/man1/`
: Man pages, including those installed for programs built with `tomo build
--install`.
