# Tomo - Tomorrow's Language

Tomo is a statically typed, safe, simple, lightweight, efficient programming
language that cross-compiles to C. Tomo is designed to anticipate and influence
the language design decisions of the future.

```
func greeting(name:Text, add_exclamation:Bool -> Text):
    message := "hello $name"
    message = " ":join([w:title() for w in message:split($/{space}/)])
    if add_exclamation:
        message ++= "!!!"
    return message

func main(name:Text, shout=no):
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
[examples/](examples/).

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
  checking](docs/arrays.md), and no uninitialized variables)
- High-performance [arbitrary-precision integers](docs/integers.md) by default
  with opt-in fixed-size integers with arithmetic overflow checking
- [Type-safe strings representing different languages](docs/langs.md) with
  automatic prevention of code injection
- Pattern matching with exhaustiveness checking for [enumerated types (tagged
  unions/enums)](docs/enums.md)
- Type-safe [optional values](docs/optionals.tm) with low syntax overhead
- Efficient datastructures with immutable value semantics:
  [arrays](docs/arrays.md), [tables](docs/tables.md), [sets](docs/sets.md),
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
  [arrays](docs/arrays.md), [tables](docs/tables.md), [sets](docs/sets.md),
  [text](docs/text.md).  
- Full-featured [libraries/modules](docs/libraries.md)
- [Full UTF8 support](docs/text.md) for all text operations
- Built-in doctests with syntax highlighting
- [Automatic command line argument parsing with type safety](docs/command-line-parsing.md)
- [Easy interoperability with C](docs/c-interoperability.md)
- Support for [POSIX threads](docs/threads.tm) and thread-safe message passing
  queues over [channels](docs/channels.tm).
- Built-in data serialization and deserialization for datatypes.

## Dependencies

Tomo has a very small set of dependencies:

- The [Boehm garbage collector](https://www.hboehm.info/gc/) for runtime
  garbage collection.
- [libunistring](https://www.gnu.org/software/libunistring/) for unicode
  string support.
- [GNU multiple precision arithmetic library](https://gmplib.org/manual/index)
  for arbitrary precision integer math.
- [Binutils](https://www.gnu.org/software/binutils/) for stack traces.
- a C compiler
- and libc/libm, which should definitely already be installed.

The Boehm GC, libunistring, and binutils should be available on your package
manager of choice (for example, `pacman -S gc libunistring binutils`).

## Building

The Tomo compiler can be compiled with either GCC or Clang by running `make`.

## Usage

Run Tomo interactively as a REPL (limited functionality):

```bash
tomo
# Starts a REPL session
```

Run a Tomo file directly:

```bash
tomo foo.tm
# Runs the program
```

Compile a Tomo file into an object file:

```bash
tomo -c foo.tm
# Output: foo.tm.o
```

Transpile a Tomo file into a C header and source file:
```bash
tomo -t foo.tm
# Outputs: foo.tm.h foo.tm.c
```

Tomo uses the environment variables (`$CC`, `$VERBOSE`, and `$AUTOFMT`), which
control the compilation/running behavior of Tomo. The default behavior is to
use `tcc` (Tiny C Compiler) for fast compilation, `VERBOSE=0`, and
`AUTOFMT='indent -kr -l100 -nbbo -nut -sob'` for autoformatting generated code.
Any of these variables may be overridden, e.g. `CC=gcc VERBOSE=1 AUTOFMT= tomo
foo.tm` (compile with GCC and verbose compiler output without autoformatting
the code).

## Installing

```
make && sudo make install
```

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
