# Tomo - Tomorrow's Language

Tomo is a statically typed, safe, simple, lightweight, efficient programming
language that cross-compiles to C. Tomo is designed to anticipate and influence
the language design decisions of the future.

```
func greeting(name:Text)->Text
	greeting := "hello {name}!"
	return greeting:title()

>> greeting("world")
= "Hello World!"
```

## Features

- Extremely high performance code generation with minimal overhead compared to C
- Extremely fast parallel compilation times
- Memory safety (garbage collection, compiler-enforced null safety, automatic
	array bounds checking, and no uninitialized variables)
- Arithmetic overflow checking
- Simple, low-boilerplate type system with type inference
- Useful and efficient built-in types: arrays, hash tables, structs, tagged
	unions (sum types), cords (efficient string representation)
- Well-defined reference and value semantics and mutability rules
- Language-level support for out-of-the-box function caching emphasizing
	correctness
- Type-safe strings representing different languages with automatic prevention
	of code injection
- Full UTF8 support for both source code and standard library
- Pattern matching with exhaustiveness checking for tagged unions
- Beautiful and helpful compiler and runtime error messages with emphasis on
	user-friendliness
- Structs with known-at-compile-time methods, not OOP objects with vtable
	lookups
- Built-in doctests with syntax highlighting
- Easy interoperability with C

## Dependencies

Tomo has a very small set of dependencies:

- The [Boehm garbage collector](https://www.hboehm.info/gc/) for runtime
	garbage collection.
- [libunistring](https://www.gnu.org/software/libunistring/) for unicode
	string support.
- a C compiler
- and libc/libm, which should definitely already be installed.

Both of which should be available on your package manager of choice (for
example, `pacman -S gc libunistring`).

## Building

The Tomo compiler can be compiled with either GCC or Clang by running `make`.

## Running

You can run a Tomo program by running `./tomo program.tm`. By default, this
will use your environment's `$CC` variable to select which C compiler to use.
If no C compiler is specified, it will default to `tcc` (Tiny C Compiler),
which is exceptionally fast.

## Installing

```
make && sudo make install
```
