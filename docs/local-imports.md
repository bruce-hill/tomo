# Local Imports

In Tomo, you can split a project into multiple files and access the code in
other files by doing `use ./file.tm`. There are a few major benefits to doing
this:

1. Files can be split apart at conceptual boundaries to make the codebase
   easier to read and navigate.
2. Each file has its own namespace and can define local variables not visible
   outside the file. This makes it easy to avoid namespace collisions.
3. Tomo compiles each file in parallel with lazy recompliation. This means that
   compile times can be much faster if a project is intelligently split into
   multiple files.

To see how local imports work, let's look at a simple file:

```tomo
// File: foo.tm
my_variable := "hello"
```

When this file is compiled to a static object file by `tomo build --obj foo.tm`, it
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

```tomo
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
