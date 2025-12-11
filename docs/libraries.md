# Tomo Library/Module Design

There are two ways to "import" code that is defined elsewhere: local files from
the same project and shared library objects from another project. The first
type of import (local files) is necessary for splitting large projects into
smaller components for ease of understanding and compilation speed. The second
type of import (shared libraries) is to allow you to install third party
libraries or frameworks that can be used across many projects.

## Local Imports

To see how local imports work, let's look at a simple file:

```
// File: foo.tm
my_variable := "hello"
```

When this file is compiled to a static object file by `tomo -c foo.tm`, it
produces the following C header file and C source file:

```c
// File: foo.tm.h
#pragma once
#include <tomo/tomo.h>

extern Text_t my_variable$foo_C3zxCsha;
void $initialize$foo_C3zxCsha(void);
```

```c
// File: foo.tm.c
#include <tomo/tomo.h>
#include "foo.tm.h"

public Text_t my_variable$foo_C3zxCsha = Text("hello");
public void $initialize$foo_C3zxCsha(void) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
}
```

Notice that the symbols defined here (`my_variable$foo_C3zxCsha`) use a
filename-based suffix with a random bit at the end that includes a dollar sign.
C compilers support an extension that allows dollar signs in identifiers, and
this allows us to use guaranteed-unique prefixes so symbols from one file don't
have naming collisions with symbols in another file.

The C file is compiled by invoking the C compiler with something like: `cc
<flags...> -c foo.tm.c -o foo.tm.o`

Now, what happens if we want to _use_ the compiled object file?

```
// File: baz.tm
foo := use ./foo.tm

func say_stuff()
    say("I got $(foo.my_variable) from foo")

func main()
    say_stuff()
```

If I want to run `baz.tm` with `tomo baz.tm` then this transpiles to:

```c
// File: baz.tm.h
#pragma once
#include <tomo/tomo.h>
#include "./foo.tm.h"

void say_stuff$baz_VEDjfzDs();
void main$baz_VEDjfzDs();
void $initialize$baz_VEDjfzDs(void);
```

```c
// File: baz.tm.c
#include <tomo/tomo.h>
#include "baz.tm.h"

public void say_stuff$baz_VEDjfzDs() {
    say(Texts(Text("I got "), my_variable$foo_C3zxCsha, Text(" from foo")), yes);
}

public void main$foo_VEDjfzDs() {
    say_stuff$foo_VEDjfzDs();
}

public void $initialize$foo_VEDjfzDs(void) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    $initialize$foo_C3zxCsha();
    ...
}

int main$baz_VEDjfzDs$parse_and_run(int argc, char *argv[]) {
    tomo_init();
    $initialize$baz_VEDjfzDs();

    Text_t usage = Texts(Text("Usage: "), Text$from_str(argv[0]), Text(" [--help]"));
    tomo_parse_args(argc, argv, usage, usage);
    main$baz_VEDjfzDs();
    return 0;
}
```

The automatically generated function `main$baz_VEDjfzDs$parse_and_run` is in
charge of parsing the command line arguments to `main()` (in this case there
aren't any) and printing out any help/usage errors, then calling `main()`.

Then `baz.tm.o` is compiled to a static object with `cc <flags...> -c baz.tm.c
-o baz.tm.o`.

Next, we need to create an actual executable file that will invoke
`main$baz_VEDjfzDs$parse_and_run()` (with any command line arguments). To do
that, we create a small wrapper program:

```c
// File: /tmp/program.c
#include <tomo/tomo.h>
#include "baz.tm.h"
 
int main(int argc, char *argv[])
{
    return main$baz_VEDjfzDs$parse_and_run(argc, argv);
}
```

This program is compiled with the already-built object files to produce an
executable binary called `foo` like this: `cc <flags...> /tmp/program.c
foo.tm.o baz.tm.o -o baz`

Finally, the resulting binary can be executed to actually run the program!


## Shared Library Imports

In Tomo, a shared library is built out of a *directory* that contains multiple
`.tm` files. Each `.tm` file in the directory (excluding those that start with
an underscore) will be compiled and linked together to produce a single
`libwhatever.so` file (or `libwhatever.dylib` on Mac) and `whatever.h` file
that can be used by other Tomo projects. You can build a library by running
`tomo -L /path/to/dir` or `tomo -L` in the current directory.

### Installing

If you additionally add the `-I` flag, Tomo will copy the entire directory
(excluding files and directories that begin with `.` such as `.git`) into
`~/.local/lib/tomo@vTOMO_VERSION/LIBRARY_NAME@LIBRARY_VERSION`.

### Using Shared Libraries

To use a shared library, write a statement like `use foo` with an unqualified
name (i.e. not an absolute or relative path like `/foo` or `./foo`). When a
program uses a shared library, that shared library gets dynamically linked to
the executable when compiling, and all of the necessary symbol information is
read from the source files during compilation.

### Versioning

When you build and install a library, its version is determined from a
`CHANGES.md` file at the top level of the library directory (see:
[Versions](versions.md)). The library's version number is added to the file
path where the library is installed, so if the library `mylib` has version
`v1.2`, then it will be installed to
`~/.local/lib/tomo@TOMO_VERSION/mylib@v1.2/`. When using a library, you must
explicitly supply either the exact version in the `use` statement like this:
`use mylib@v1.2`, or provide a `modules.ini` file that lists version
information and other details about modules being used. For each module, you
should provide a `[modulename]` section with a `version=` field.

```tomo
# File: foo.tm
use mylib
...
```

And the accompanying `modules.ini`:

```ini
[mylib]
version=v1.2
```

The `modules.ini` file must be in the same directory as the source files that
use its aliases, so if you want to share a `modules.ini` file across multiple
subdirectories, use a symbolic link. If you need to include per-file overrides
for a directory's `modules.ini` file, you can use `foo.tm:modules.ini`.

### Module Downloading

If you want, you can also provide the following options for a module:

- `git`: a Git URL to clone the repository
- `revision`: if a Git URL is provided, use this revision
- `url`: a URL to download an archive of the library (`.zip`, `.tar`, `.tar.gz`)
- `path`: if the library is provided in a subdirectory of the repository or
  archive, list the subdirectory here.

For example, this is what it would look like to use the `colorful` library that
is distributed with the Tomo compiler in the `examples/colorful` subdirectory:

```ini
[colorful]
version=v1.0
git=git@github.com:bruce-hill/tomo
path=examples/colorful
```

If this extra information is provided, Tomo will prompt the user to ask if they
want to download and install this module automatically when they run a program
and don't have the necessary module installed.
