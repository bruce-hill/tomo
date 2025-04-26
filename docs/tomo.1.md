% TOMO(1)
% Bruce Hill (*bruce@bruce-hill.com*)
% June 11, 2024

# NAME

tomo - The programming language of tomorrow.

# SYNOPSIS

Run a program:
: `tomo` *program.tm* \[\[`--`\] *args...*\]

Transpile tomo files to C files:
: `tomo` `-t` *file1.tm* *file2.tm*...

Compile files to static object files:
: `tomo` `-c` *file1.tm* *file2.tm*...

Compile file to an executable:
: `tomo` `-e` *file1.tm*

Build a shared library:
: `tomo` `-s=`*mylib.1.2.3* *file1.tm* *file2.tm*...

# DESCRIPTION

Tomo is a programming language that is statically typed, compiled, small, and
garbage-collected, with concise syntax and built-in support for
high-performance, low-overhead datastructures. It compiles by first outputting
C code, which is then compiled using a C compiler of your choice.

# OPTIONS

`-h`, `--help`
: Print the usage and exit.

`-t`, `--transpile`
: Transpile the input files to C code without compiling them.

`-c`, `--compile-obj`
: Compile the input files to static objects, rather than running them.

`-e`, `--compile-exe`
: Compile the input file to an executable.

`-L`, `--library`
: Compile the input files to a shared library file and header.

`-I`, `--install`
: Install the compiled executable or library.

`-C` *<program>*, `--show-codegen` *<program>*
: Set a program (e.g. `cat` or `bat`) to display the generated code

`--c-compiler`
: Set which C compiler is used.

`-O` **level**, `--optimization` **level**
: Set the optimization level.

`-v`, `--verbose`
: Print extra verbose output.

`--version`
: Print the compiler version and exit.

`-r`, `--run`
: Run an installed tomo program from `~/.local/share/tomo/installed`.

## ENVIRONMENT VARIABLES

Some options can be configured by setting environment variables.

`CC=`*c-compiler*
: Set which C compiler is used.
