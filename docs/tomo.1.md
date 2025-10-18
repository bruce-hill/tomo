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

Compile files to a executables:
: `tomo` `-e` *file1.tm* *file2.tm*...

Build a library:
: `tomo` `-L` *file1.tm* *file2.tm*...

# DESCRIPTION

Tomo is a programming language that is statically typed, compiled, small, and
garbage-collected, with concise syntax and built-in support for
high-performance, low-overhead datastructures. It compiles by first outputting
C code, which is then compiled using a C compiler of your choice.

# OPTIONS

`--changelog`
: Print the compiler change log and exit.

`--transpile`, `-t` *file1.tm* *file2.tm*...
: Transpile the given files to C.

`--compile-exe`, `-e` *file1.tm* *file2.tm*...
: Compile the given files to executables.

`--compile-obj`, `-c` *file1.tm* *file2.tm*...
: Compile the given files to static objects.

`--help`, `-h`
: Print the usage and exit.

`--library`, `-L` *folder1* *folder2*...
: Compile the input folders to shared libraries.

`--install`, `-I`
: When using `-e` or `-L`, install the resulting executables or libraries.

`--show-codegen` *<program>*, `-C` *<program>*
: Set a program (e.g. `cat` or `bat`) to display the generated code

`--force-rebuild`, `-f`
: Force rebuilding/recompiling.

`--format` *file1.tm* *file2.tm*...
: Autoformat the given files and print them to standard output.

`--format` *file1.tm* *file2.tm*...
: Autoformat the given files in-place and overwrite the original files.

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

`--uninstall`, `-u` *lib1* *lib2*...
: Uninstall the given libraries.

`--verbose`, `-v`
: Print extra verbose output.

`--version`
: Print the compiler version and exit.
