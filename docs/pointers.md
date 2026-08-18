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

```tomo
optional := @[10, 20]?
```

The compiler will not allow you to dereference an optionally null pointer
without explicitly checking for null. To do so, use a conditional check like
this, and everywhere inside the truthy block will allow you to use the pointer
as a non-null pointer:

```tomo
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

# Variable References and Non-escaping Pointers

Tomo allows you to take the reference of a local variable or value similarly to
C using the `&` operator. The result is a pointer with type `&T`:

```tomo
x := 123
ptr := &x
assert ptr[] == 123
ptr[] = 456
assert x == 456
```

This can be helpful for making functions that modify local variables:

```tomo
func increment(ptr:&Int)
    ptr[] += 1

x := 123
increment(&x)
assert x == 124
```

You can automatically promote a `@` pointer (heap) to a `&` pointer
(stack-or-heap):

```tomo
# Valid promotion:
heap := @123
increment(heap)
assert heap[] == 124
```

However, there are several restrictions on how `&` pointers are used:

1. A stack-or-heap `&` pointer cannot be returned from a function (e.g. `return &x`)
2. A stack-or-heap `&` pointer cannot be stored in memory (e.g. `arr[i] = &x`)
3. A stack-or-heap `&` pointer cannot be promoted to a heap `@` pointer

These restrictions ensure that a local variable reference can't outlive the
stack frame in which it was created, which would be a memory safety error.

So, the general rule is that heap `@` pointers are for long-lived or
interlinked data and stack-or-heap `&` pointers are for local variable
references or functions that are agnostic to where the data lives. Functions
that accept `&` pointers are more flexible in what they can accept as inputs,
but they are subject to the restrictions above. When possible, it's preferable
to take `&` arguments to allow them to be called with local variable
references.

`&` references also appear as loop variables: `for &x in list` iterates a
mutable list with a reference to each element, so the elements can be updated
in place. See [Updating Elements In-Place](lists.md#updating-elements-in-place)
for details.
