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

`--changelog`
: Print the compiler change log and exit.

`--compile-exe`, `-e`
: Compile the input file to an executable.

`--compile-obj`, `-c`
: Compile the input files to static objects, rather than running them.

`--help`, `-h`
: Print the usage and exit.

`--install`, `-I`
: Install the compiled executable or library.

`--library`, `-L`
: Compile the input files to a shared library file and header.

`--show-codegen` *<program>*, `-C` *<program>*
: Set a program (e.g. `cat` or `bat`) to display the generated code

`--force-rebuild`, `-f`
: Force rebuilding/recompiling.

`--format`
: Autoformat a file and print it to standard output.

`--format-inplace`
: Autoformat a file in-place.

`--optimization` **level**, `-O` **level**
: Set the optimization level.

`--prefix`
: Print the Tomo installation prefix and exit.

`--quiet`, `-q`
: Run in quiet mode.

`--run`, `-r`
: Run an installed tomo program from `~/.local/lib/tomo_vX.Y/`.

`--source-mapping=`, `-m=` **<yes|no>**
: Toggle whether source mapping should be enabled or disabled.

`--transpile`, `-t`
: Transpile the input files to C code without compiling them.

`--uninstall`, `-u`
: Uninstall a compiled executable or library.

`--verbose`, `-v`
: Print extra verbose output.

`--version`
: Print the compiler version and exit.
