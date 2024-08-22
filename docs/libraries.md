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

extern Text_t foo$my_variable;
```

```c
// File: foo.tm.c
#include <tomo/tomo.h>
#include "foo.tm.h"

Text_t foo$my_variable = "hello";
```

Notice that the symbols defined here (`foo$my_variable`) use a file-based
prefix that includes a dollar sign. C compilers support an extension that
allows dollar signs in identifiers, and this allows us to use guaranteed-unique
prefixes so symbols from one file don't have naming collisions with symbols
in another file.

The C file is compiled by invoking the C compiler with something like: `cc
<flags...> -c foo.tm.c -o foo.tm.o`

Now, what happens if we want to _use_ the compiled object file?

```
// File: baz.tm
foo := use ./foo.tm

func say_stuff():
    say("I got {foo.my_variable} from foo")

func main():
    say_stuff()
```

If I want to run `baz.tm` with `tomo baz.tm` then this transpiles to:

```c
// File: baz.tm.h
#pragma once
#include <tomo/tomo.h>
#include "./foo.tm.h"

void baz$say_stuff();
void baz$main();
```

```c
// File: baz.tm.c
#include <tomo/tomo.h>
#include "baz.tm.h"

public void baz$say_stuff()
{
  say(CORD_all("I got ", foo$my_variable, " from foo"));
}

public void baz$main()
{
    baz$say_stuff();
}
```

Then `baz.tm.o` is compiled to a static object with `cc <flags...> -c baz.tm.c
-o baz.tm.o`.

Next, we need to create an actual executable file that will invoke `baz$main()`
(with any command line arguments). To do that, we create a small wrapper
program:

```c
// File: /tmp/program.c
#include <tomo/tomo.h>
#include "baz.tm.h"
 
int main(int argc, char *argv[])
{
    tomo_init();
    if (argc > 1)
        errx(1, "This program doesn't take any arguments.");
    baz$main();
    return 0;
}
```

This program is compiled with the already-built object files to produce an
executable binary called `foo` like this: `cc <flags...> /tmp/program.c
foo.tm.o baz.tm.o -o baz`

Finally, the resulting binary can be executed to actually run the program!


## Shared Library Imports

Now, what's the story with shared library imports? The equivalent process for C
is to create a `.so` or `.dll` file. In order to build a shared library, you
run the command `tomo -s=qux.1.2.3 file1.tm file2.tm...`. Each specified file
will have its `.o` static object file compiled, along with its dependencies,
and all of the resulting `.o` files will be linked together by `tomo` with a
command like `cc <flags...> -Wl,-soname=libqux.1.2.3.so -shared file1.tm.o
file2.tm.o dep1.tm.o ... -o libqux.1.2.3.so`. The specified files must not
define the same public symbols as each other, since `foo` will now be treated
as a single namespace that holds all the symbols from each of the given files.

### Symbol Uniqueness

In the future, each of these symbols will be given an extra prefix to prevent
namespace collisions and a standalone header file will be built that defines
every public symbol in the library in a way that a C compiler can understand.
In our example, running `tomo -s=qux.1.2.3 foo.tm baz.tm` would produce a
header file like this:

```c
#pragma once
#include <tomo.h>

extern Text_t qux$1$2$3$foo$my_variable;
extern void qux$1$2$3$baz$say_stuff();
```

### Installing

Now, the components necessary to install this shared library on your computer
are these:

- The `.so` file, installed in a standard location. In our case, we will default
  to `~/.local/share/tomo/lib/libqux.so`
- The standalone `.h` file, installed in a standard location. We default to
  `~/.local/share/tomo/include/qux.h`
- All of the source `.tm` files (which store type information necessary for tomo
  to understand what's being imported. These will be installed to
  `~/.local/share/tomo/src/qux/`

### Using Shared Libraries

To use a shared library, write a statement like `use qux` with an unqualified
name (i.e. not an absolute or relative path like `/qux` or `./qux`). When a
program uses a shared library, that shared library gets dynamically linked to
the executable when compiling, and all of the necessary symbol information is
read from the source files during compilation.

### Library Versioning

In order to accommodate multiple versions of the same shared libraries on a
system, users may specify a library version when compiling, for example: `tomo
-s qux.1.2.3 foo.tm baz.tm` During installation, symlinks are created to map
less specific version numbers to more specific version numbers. For example,
when installing `qux.1.2.3`, links are created:

- `~/.local/share/lib/tomo/libqux.1.2.so` -> `~/.local/share/lib/tomo/libqux.1.2.3.so`
- `~/.local/share/lib/tomo/libqux.1.so` -> `~/.local/share/lib/tomo/libqux.1.2.3.so`
- `~/.local/share/lib/tomo/libqux.so` -> `~/.local/share/lib/tomo/libqux.1.2.3.so`
- And so on for `include/tomo/libqux.1.2.3.h` and `src/tomo/qux.1.2.3/`

If there are multiple versions (e.g. `1.2.3` and `1.3.0`), then links point at
the highest-numbered version with the necessary prefix. In this case, `qux ->
qux.1.3.0`, `qux.1 -> qux.1.3.0`, `qux.1.3 -> qux.1.3.0`, and `qux.1.2 ->
qux.1.2.3`.

