% TOMO(1)
% Bruce Hill (*bruce@bruce-hill.com*)
% June 11, 2024

# NAME

tomo - The programming language of tomorrow.

# SYNOPSIS

Run the REPL:
: `tomo`

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

`-t`
: Transpile the input files to C code without compiling them.

`-c`
: Compile the input files to static objects, rather than running them.

`-e`
: Compile the input file to an executable.

`-L`
: Compile the input files to a library `.so` file and header.

`-I`
: Install the compiled executable or library.

## ENVIRONMENT VARIABLES

Some options can be configured by setting environment variables.

`VERBOSE=0|1|2`
: Set the verbosity level.

`CC=`*c-compiler*
: Set which C compiler is used.

`O=`*optimization-level*
: Set which optimization level is passed to the C compiler. Valid values are
  those accepted by your compiler's `-O` flag.

`AUTOFMT=`*autoformatter*
: The program used to autoformat generated C code. Default: `indent -kr -l100 -nbbo -nut -sob`
