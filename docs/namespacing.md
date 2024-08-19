# Namespacing

In order to work with C's namespace limitations, I've designed the following
system:

## Namespaces

In C, there is a GCC extension (also supported by clang and TCC) to allow for
dollar signs in identifiers. This provides a way to have compiled C code which
segments its imports into different namespaces. For example `Foo$Baz` would be
the identifier `Baz` in the namespace `Foo`, and would be guaranteed to not
collide with a user-chosen name like `FooBaz`.

```
// File: foo.nl
struct Baz(x:Int):
    member := 5
    func frob(b:Baz)->Int:
        return b.x

qux := "Loaded!"
say(qux)

// File: foo.nl.h
...
typedef struct foo$Baz_s foo$Baz_t;
#define foo$Baz(...) (foo$Baz_t){__VA_ARGS__}
struct foo$Baz_s {
    Int_t x;
};
extern Int_t foo$Baz$member;
Int_t foo$Baz$frob(foo$Baz_t b);
...

// File: foo.nl.c
#include "foo.nl.h"
Int_t foo$Baz$member = 5;

Int_t foo$Baz$frob(foo$Baz_t b) {
    return b.x;
}

void use$foo(void) {
    static enum {UNLOADED, LOADING, LOADED} $state = UNLOADED;
    if ($state == LOADING)
        fail("Circular import");
    else if ($state == LOADED)
        return;

    $state = LOADING;
    { // Top-level code:
        Str qux = "Loaded!";
        say(qux);
    }
    $state = LOADED;
}
```

And on the usage site, the code `use ./foo.tm` compiles to `#include
"./foo.tm.h"` in the header and `use$foo()` in the code that is executed.

