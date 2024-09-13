# Namespacing

In order to work with C's namespace limitations, I've designed the following
system:

## Namespaces

In C, there is a GCC extension (also supported by clang and TCC) to allow for
dollar signs in identifiers. This provides a way to have compiled C code which
segments its imports into different namespaces. For example `Foo$Baz` would be
the identifier `Baz` in the namespace `Foo`, and would be guaranteed to not
collide with a user-chosen name like `FooBaz`.

```tomo
// File: foo.tm
my_var := 123

struct Baz(x:Int):
    member := 5
    func frob(b:Baz)->Int:
        return b.x
```

```C
// File: foo.tm.h
...
typedef struct foo$Baz_s foo$Baz_t;
struct foo$Baz_s {
    Int_t $x;
};

extern Int_t foo$my_var;
extern const TypeInfo foo$Baz;

extern Int_t foo$Baz$member;
Int_t foo$Baz$frob(struct foo$Baz_s $b);
void foo$main();
...
```

```C
// File: foo.tm.c
...
Int_t foo$my_var = I_small(123);
Int_t foo$Baz$member = I_small(5);

static Text_t foo$Baz$as_text(foo$Baz_t *obj, bool use_color)
{
    if (!obj)
        return "Baz";
    return Texts(use_color ? Text("\x1b[0;1mBaz\x1b[m(") : Text("Baz("),
                    Int$as_text(stack(obj->$x), use_color, &Int$info), Text(")"));
}

public Int_t foo$Baz$frob(struct foo$Baz_s $b)
{
    return ($b).$x;
}
...
```

And on the usage site, the code `include ./foo.tm` compiles to `#include
"./foo.tm.h"` in the header and `use$foo()` in the code that is executed.

