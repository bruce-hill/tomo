# Namespacing

In order to work with C's namespace limitations, I've designed the following
system, which makes use of a C language extension `-fdollars-in-identifiers`
that lets you use dollar signs in identifiers. This extension is supported by
GCC, TinyCC, and Clang.

## Unique File Suffixes

Each file gets a unique suffix with the format `$<filename>_XXXXXXXX`, where the
Xs are 8 randomly chosen identifier characters and `<filename>` includes only
valid identifier characters up to the first period.

For example, in a file called `hello-world.tm`, a variable like `foo` would
become `foo$helloworld_VEDjfzDs`. This helps avoid namespace conflicts between
two files that define the same symbol.

## Namespaces

Dollar signs in identifiers provide a way to have compiled C code which segments
its imports into different namespaces. For example `Foo$Baz` would be the
identifier `Baz` in the namespace `Foo`, and would be guaranteed to not collide
with a user-chosen name like `FooBaz` or `Foo_Baz`.

## Example

For this Tomo code:

```tomo
// File: foo.tm
my_var := 123

struct Baz(x:Int)
    member := 5
    func frob(b:Baz -> Int)
        return b.x

func main() pass
```

The generated C source code will look like this:

```C
// File: .build/foo.tm.h
...
typedef struct Baz$$struct$foo_VEDjfzDs Baz$$type$foo_VEDjfzDs;
struct Baz$$struct$foo_VEDjfzDs {
    Int_t x;
};
DEFINE_OPTIONAL_TYPE(struct Baz$$struct$foo_VEDjfzDs, 8,$OptionalBaz$$type$foo_VEDjfzDs);
extern const TypeInfo_t Baz$$info$foo_VEDjfzDs;
extern Int_t Baz$member$foo_VEDjfzDs;
Int_t Baz$frob$foo_VEDjfzDs(struct Baz$$struct$foo_VEDjfzDs _$b);
extern Int_t my_var$foo_VEDjfzDs;
void main$foo_VEDjfzDs();
...
```

```C
// File: .build/foo.tm.c
...
public Int_t my_var$foo_VEDjfzDs = I_small(123);
public const TypeInfo_t Baz$$info$foo_VEDjfzDs = {...};
public Int_t Baz$member$foo_VEDjfzDs = I_small(5);

public Int_t Baz$frob$foo_VEDjfzDs(struct Baz$$struct$foo_VEDjfzDs _$b) {
    return (_$b).x;
}

public void main$foo_VEDjfzDs() {
}
...
```
