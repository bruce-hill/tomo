# Pointers

Pointers are numeric values that represent a location in memory where some type
of data lives. Pointers are created using the `@` prefix operator to
**a**llocate heap memory.

Pointers are the way in Tomo that you can create mutable data. All
datastructures are by default, immutable, but using pointers, you can create
a region of memory where different immutable values can be held, which change
over time. Essentially, you can think about mutation as the act of creating
a new, different value and assigning it to a pointer's memory location to
replace the value that previously resided there.

```tomo
func no_mutation_possible(nums:[Int])
    nums[1] = 10 // This performs a copy-on-write and creates a new list
    // The new list is only accessible as a local variable here
...
my_nums := [0, 1, 2]
no_mutation_possible(my_nums)
assert my_nums == [0, 1, 2]

func do_mutation(nums:@[Int])
    nums[1] = 10 // The mutates the value at the given pointer's location
...
my_nums := @[0, 1, 2]
do_mutation(my_nums)
assert my_nums == @[10, 1, 2]
```

## Dereferencing

Pointers can be dereferenced to access the value that's stored at the pointer's
memory location using the `[]` postfix operator (with no value inside).

```tomo
nums := @[10, 20]
assert nums[] == [10, 20]
```

## Equality and Comparisons

When comparing two pointers, the comparison operates on the _memory address_,
not the contents of the memory. This is "referential" equality, not
"structural" equality. The easy way to think about it is that two pointers are
equal to each other only if doing a mutation to one of them is the same as
doing a mutation to the other.

```tomo
x := @[10, 20, 30]
y := @[10, 20, 30]
assert x != y

z := x
assert x == z
```

Pointers are ordered by memory address, which is somewhat arbitrary, but
consistent.

## Null Safety

Tomo pointers are, by default, guaranteed to be non-null. If you write a
function that takes a `@T`, the value that will be given is always non-null.
However, optional pointers can be used by adding a question mark to the type:
`@T?`. A null value can be created using the syntax `!@T`. You can also append
a question mark to a pointer value so the type checker knows it's supposed to
be optional:

```
optional := @[10, 20]?
```

The compiler will not allow you to dereference an optionally null pointer
without explicitly checking for null. To do so, use a conditional check like
this, and everywhere inside the truthy block will allow you to use the pointer
as a non-null pointer:

```
if optional
    ok := optional[]
else
    say("Oh, it was null")
```

## Using Pointers

For convenience, most operations that work on values can work with pointers to
values implicitly. For example, if you have a struct type with a `.foo` field,
you can use `ptr.foo` on a pointer to that struct type as well, without needing
to use `ptr[].foo`. The same is true for list accesses like `ptr[i]` and method
calls like `ptr.reversed()`.

# Read-Only Views

In a small number of API methods (`list.first()`, `list.binary_search()`,
`list.sort()`, `list.sorted()`, and `list.heapify()`), the methods allow you
to provide custom comparison functions. However, for safety, we don't actually
want the comparison methods to be able mutate the values inside of immutable
list values. For implementation reasons, we can't pass the values themselves
to the comparison functions, but need to pass pointers to the list members.
So, to work around this, Tomo allows you to define functions that take
immutable view pointers as arguments. These behave similarly to `@` pointers,
but their type signature uses `&` instead of `@` and read-only view pointers
cannot be used to mutate the contents that they point to and cannot be stored
inside of any datastructures as elements or members.

```tomo
nums := @[10, 20, 30]
assert nums.first(func(x:&Int): x / 2 == 10) == 2
```

Normal `@` pointers can be promoted to immutable view pointers automatically,
but not vice versa.
