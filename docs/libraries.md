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
    say(Texts(Text("I got "), foo$my_variable, Text(" from foo")));
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

In Tomo, a shared library is built out of a *directory* that contains multiple
`.tm` files. Each `.tm` file in the directory (excluding those that start with
an underscore) will be compiled and linked together to produce a single
`libwhatever.so` file and `whatever.h` file that can be used by other Tomo
projects. You can build a library by running `tomo -L dirname/` or `tomo -L` in
the current directory.

### Installing

If you additionally add the `-I` flag, Tomo will copy the entire directory
(excluding files and directories that begin with `.` such as `.git`) into
`~/.local/share/tomo/installed/` and create a symbolic link for the library's
`.so` file in `~/.local/share/tomo/lib/`.

### Using Shared Libraries

To use a shared library, write a statement like `use foo` with an unqualified
name (i.e. not an absolute or relative path like `/foo` or `./foo`). When a
program uses a shared library, that shared library gets dynamically linked to
the executable when compiling, and all of the necessary symbol information is
read from the source files during compilation.
