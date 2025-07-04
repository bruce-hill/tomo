# Tomo - Tomorrow's Language

Tomo is a statically typed, safe, simple, lightweight, efficient programming
language that cross-compiles to C. Tomo is designed to anticipate and influence
the language design decisions of the future.

Please visit [tomo.bruce-hill.com](https://tomo.bruce-hill.com) for full
documentation.

```
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
$ tomo -e hello.tm
$ ./hello "john doe" --shout
Hello John Doe!!!
```

For more examples, see [learnXinY](/examples/learnxiny.tm) which as an overview
of many language features or the other example programs/modules in
[examples/](examples/). You can also look at the [core libraries](lib/) which
are implemented in Tomo.

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
- Full-featured [libraries/modules](docs/libraries.md)
- [Full UTF8 support](docs/text.md) for all text operations
- Built-in doctests with syntax highlighting
- [Automatic command line argument parsing with type safety](docs/command-line-parsing.md)
- [Easy interoperability with C](docs/c-interoperability.md)
- Built-in [data serialization and deserialization](docs/serialization.md).

## Dependencies

Tomo has a very small set of dependencies:

- GCC version 12 or higher (might work on lower versions, but has not been tested)
- The [Boehm garbage collector](https://www.hboehm.info/gc/) for runtime
  garbage collection.
- [libunistring](https://www.gnu.org/software/libunistring/) for unicode
  string support (version 1.0 or higher)
- [GNU multiple precision arithmetic library](https://gmplib.org/manual/index)
  for arbitrary precision integer math (version 6.2.1 or higher)
- [Binutils](https://www.gnu.org/software/binutils/) to use `addr2line` for
  stack traces.
- and libc/libm, which should definitely already be installed.

If you're feeling incautious, you can run `make deps` or
`./install_dependencies.sh` to install all the necessary dependencies. I can't
guarantee this works on all platforms, but has a reasonably high chance of
success.

## Building

The Tomo compiler can be compiled with either GCC or Clang by running `make`.
The first time you build, you will need to specify the Tomo installation
location (the place where the installer will put the `tomo` binary, the shared
library, and the header files), the Tomo home directory (where Tomo libraries
will be installed), and the default C compiler for Tomo to use when compiling
and running your programs. The answers you give will be saved in `config.mk`,
which you can edit at any time.

Once the configuration options have been set, `make` will continue along with
building `tomo`. The resulting compiler and shared library will be put into
`./build/`.

You can run `make test` to verify that everything works correctly.

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

To run a Tomo file directly:

```bash
tomo foo.tm
```

To compile a Tomo file into an object file:

```bash
tomo -c foo.tm
# Output: .build/foo.tm.o
```

To transpile a Tomo file into a C header and source file:

```bash
tomo -t foo.tm
# Outputs: .build/foo.tm.h .build/foo.tm.c
```

You can see the full list of compiler options by running `man tomo` or `tomo
--help`.


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
