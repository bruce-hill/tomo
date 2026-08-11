# Tomo - Tomorrow's Language

![](tomo.svg)

Tomo is a statically typed, safe, simple, lightweight, efficient programming
language that cross-compiles to C. Tomo is designed to anticipate and influence
the language design decisions of the future.

Please visit [tomo.bruce-hill.com](https://tomo.bruce-hill.com) for full
documentation.

```tomo
func greeting(name:Text, add_exclamation:Bool -> Text)
    message := "hello $name"
    message = " ".join([w.title() for w in message.split_any(" ")])
    if add_exclamation
        message ++= "!!!"
    return message

func main(name:Text, shout=no)
    to_say := greeting(name, add_exclamation=shout)
    say(to_say)
```

```bash
$ tomo hello.tm -- world
Hello World
$ tomo hello.tm -- --name=åke
Hello Åke
$ tomo build hello.tm
$ ./hello "john doe" --shout
Hello John Doe!!!
```

For more examples, see [learnXinY](/examples/learnxiny.tm) which as an overview
of many language features or the other example programs/packages in
[examples/](examples/).

## Quick Installation

Prebuilt archives are fully self-contained (they bundle the Zig toolchain and
all needed libraries) and extract directly into an install prefix such as
`~/.local` or `/usr/local`:

```
platform="$(uname -m | sed 's/^arm64$/aarch64/;s/^amd64$/x86_64/')-$(uname -s | sed 's/Darwin/macos/' | tr 'A-Z' 'a-z')"
curl -L "https://tomo.bruce-hill.com/dist/tomo@latest-$platform.tar.xz" \
  | tar -xJ -C ~/.local
```

Available platforms: x86_64/aarch64/riscv64/powerpc64le/s390x Linux (fully
static), x86_64/aarch64 macOS, and x86_64/aarch64 FreeBSD, NetBSD, and
OpenBSD.

### Arch User Repository (AUR)

```
yay -Sy tomo-bin
```

## Features

### Performance

- Generates performant C code with minimal overhead that runs as fast as C
  code, because it *is* C code.
- Extremely fast [incremental and parallel compilation](docs/compilation.md)
- Language-level support for [correct function caching](docs/functions.md)
- [Structs](docs/structs.md) with known-at-compile-time methods, not OOP
  objects with vtable lookups

### Safety

- Memory safety (garbage collection, [compiler-enforced null
  safety](docs/pointers.md), [automatic array bounds
  checking](docs/lists.md), and no uninitialized variables)
- High-performance [arbitrary-precision integers](docs/integers.md) by default
  with opt-in fixed-size integers with arithmetic overflow checking
- [Type-safe strings representing different languages](docs/langs.md) with
  automatic prevention of code injection
- Pattern matching with exhaustiveness checking for [enumerated types (tagged
  unions/enums)](docs/enums.md)
- Type-safe [optional values](docs/optionals.tm) with low syntax overhead
- Efficient datastructures with immutable value semantics:
  [lists](docs/lists.md), [tables](docs/tables.md), [sets](docs/sets.md),
  [text](docs/text.md).
- [Privacy-protecting types](docs/structs.md#Secret-Values) that help prevent
  accidentally logging sensitive information

### Simplicity

- Simple, low-boilerplate type system with type inference
- Well-defined reference and value semantics and mutability rules
- No polymorphism, generics, or inheritance

### User-friendliness

- [String interpolation](docs/text.md) and debug printing builtins
- Built-in datastructures with a rich library of commonly used methods:
  [lists](docs/lists.md), [tables](docs/tables.md), [sets](docs/sets.md),
  [text](docs/text.md).  
- Full-featured [packages](docs/packages.md)
- [Full UTF8 support](docs/text.md) for all text operations
- Built-in debugging prints with syntax highlighting
- [Automatic command line argument parsing with type safety](docs/command-line-parsing.md)
- [Easy interoperability with C](docs/c-interoperability.md)
- Built-in [data serialization and deserialization](docs/serialization.md).
- [Paths](docs/paths.md) are a native datatype with built-in syntax and a
  user-friendly API for filesystem operations. 

## Dependencies

The Tomo compiler is built as a fully static executable (musl libc) using
`zig cc`, so it has a very small set of build dependencies:

- [Zig](https://ziglang.org/download/) (provides `zig cc`), which is used to
  compile and statically link the compiler and its vendored libraries. Only the
  host's Zig is needed to build; a pinned Zig is downloaded and bundled into the
  installation for runtime use (see below).
- [Binutils](https://www.gnu.org/software/binutils/) at build time (autoconf
  probes in the vendored libraries use the host `nm`). Runtime stack traces are
  symbolized in-process by the vendored
  [libbacktrace](https://github.com/ianlancetaylor/libbacktrace), so no
  external tool is needed at runtime.
- `curl` and `tar`/`xz` to download and unpack the vendored sources and Zig.

The Boehm garbage collector, libunistring, the GNU multiple precision
arithmetic library, and libbacktrace are vendored in `./vendor/` and built from
source statically with the same `zig cc` toolchain — no system-installed copies
of those libraries are needed. **The installed `tomo` compiles your programs
using a Zig toolchain bundled inside the installation** (under
`libexec/tomo@VERSION/zig/`), so a Tomo installation is fully self-contained: no
system C compiler is required to build or run Tomo programs, and the programs it
produces are themselves fully static musl binaries.

## Building

Tomo is built by running `make`, which builds for your current platform. The
first time you build, you will be asked for the Tomo installation location (the
place where the installer will put the `tomo` binary, the static library, the
headers, and the bundled Zig toolchain). The answer is saved in `config.mk`,
which you can edit at any time. The toolchain itself is not configurable: Tomo
is always built with `zig cc`.

`make` builds the vendored dependencies, downloads and checksum-verifies the
pinned Zig for your platform, and then builds `tomo` itself. Everything is placed
under `./build/<platform>/tomo/`. You can run `make test` to verify that
everything works correctly.

### Distribution archives

`make dist` builds a self-contained distribution archive for every platform in
the matrix (see `DIST_TARGETS` / `vendor/zig-checksums.mk`). Each archive is a
`.tar.xz` that extracts directly into an install prefix:

```
make dist
tar xf build/dist/tomo@VERSION-x86_64-linux.tar.xz -C /usr/local
```

Cross-platform archives are produced by cross-compiling Tomo and its vendored
libraries with your host Zig and bundling the target platform's native Zig
toolchain (downloaded and verified from ziglang.org). The pinned Zig version and
its per-platform SHA-256 checksums live in `vendor/zig-checksums.mk`; run
`make -C vendor download-all-zig` to mirror every platform's Zig archive.

The default matrix covers the 64-bit targets: **Linux** (x86_64, aarch64,
riscv64, powerpc64le, s390x), **macOS** (x86_64, aarch64), and the
**BSDs** (FreeBSD, NetBSD, OpenBSD; x86_64 and aarch64). Linux targets are
linked fully statically against musl libc; macOS and the BSDs cannot be
statically linked (their libc requires dynamic linking), so those bundle the
vendored libraries statically but link libc dynamically.
**Windows is excluded** because Tomo's runtime relies on POSIX facilities (fork,
mmap, pthreads, dlfcn) it doesn't provide, and **32-bit targets are excluded**
because Tomo requires a 64-bit platform. Cross-compiling to a target only
requires your host Zig — no target SDK or sysroot — since Zig bundles what's
needed to compile and link for each OS.


## Running Locally

To run Tomo locally (without installing), you can use the script
`./local-tomo`, which sets some environment variables and runs the version
of Tomo that you have built in this directory.

You can run a program like this:

```
./local-tomo my-program.tm
```

## Installing

To install Tomo, run:

```
make install
```

This will install it to the location you gave during initial configuration.
After this point, you can now run `tomo` to invoke the compiler and run your
Tomo programs.

## Usage

The simplest way to use Tomo is to run a file directly:

```bash
tomo foo.tm
# Or pass arguments to the program after "--":
tomo foo.tm -- --some-arg 'hello'
```

Running bare `tomo` with no arguments opens a scratch file in your `$EDITOR`
that runs when you save and exit, and you can also pipe a program to it
(`echo '...' | tomo`).

Everything else is a subcommand, in the style of `git` or `cargo`:

```
Usage: tomo [command] [flags...] [args...]

Commands:
  run: Compile and run Tomo programs
  build: Compile Tomo programs to standalone executables
  transpile: Transpile Tomo files to C without compiling
  parse: Print the parse tree of Tomo files as S-expressions
  fmt: Format Tomo source code
  package: Build Tomo packages into static archives
  install: Install Tomo programs and packages into TOMO_PATH
  uninstall: Remove installed Tomo programs and packages
  vendor: Copy packages' verified sources into ./vendor/
  info: Print the build info embedded in compiled binaries
  version: Print the Tomo compiler version (with -v: plus the git revision)
```

For example:

```bash
tomo build foo.tm          # Compile a standalone executable: ./foo
tomo transpile foo.tm      # Print the generated C header and source
```

Global flags like `--verbose`/`-v`, `--quiet`/`-q`, and `--optimization`/`-O`
can go anywhere on the command line; command-specific flags (like `build`'s
`-o`) go after the command name. Run `tomo --help` for the full overview,
`tomo <command> --help` for a specific command's usage, or `man tomo` for the
manpage.


## License

Tomo is provided under the Sustainable Use License (see
[LICENSE.md](LICENSE.md) for full details). This is a source-available
[fair-code](https://faircode.io) license that does not grant unlimited rights
for commercial use, but otherwise has permissive rights for noncommercial use
and allows distributing and modifying the source code. It does not comply with
the [Open Source Initiative's definition of "Open
Source"](https://opensource.org/osd), which does not allow any restrictions on
commercial use. If you would like to use this project commercially, please
contact me to work out a licensing agreement.
