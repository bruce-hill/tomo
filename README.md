# Tomo - Tomorrow's Language

Tomo is a programming language designed to anticipate and influence the
language design decisions of the future.

```
func greeting(name:Text)->Text
	greeting := "hello {name}!"
	return greeting:title()

>> greeting("world")
= "Hello World!"
```

Check out the [test/](test/) folder to see some examples.

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
