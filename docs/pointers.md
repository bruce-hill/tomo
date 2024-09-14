# Pointers

Pointers are numeric values that represent a location in memory where some type
of data lives. Pointers are created using either the `@` prefix operator to
**a**llocate heap memory or the `&` prefix operator to get the address of a
variable. Stack pointers (`&`) are more limited than heap pointers (`@`) and
cannot be stored inside an array, set, table, struct, enum, or channel.
However, stack pointers are useful for methods that mutate local variables and
don't need to save the pointer anywhere.

Pointers are the way in Tomo that you can create mutable data. All
datastructures are by default, immutable, but using pointers, you can create
a region of memory where different immutable values can be held, which change
over time. Essentially, you can think about mutation as the act of creating
a new, different value and assigning it to a pointer's memory location to
replace the value that previously resided there.

```tomo
func no_mutation_possible(nums:[Int]):
    nums[1] = 10 // This performs a copy-on-write and creates a new array
    // The new array is only accessible as a local variable here
...
my_nums := [0, 1, 2]
no_mutation_possible(my_nums)
>> my_nums
= [0, 1, 2]

func do_mutation(nums:@[Int]):
    nums[1] = 10 // The mutates the value at the given pointer's location
...
my_nums := @[0, 1, 2]
do_mutation(my_nums)
>> my_nums
= @[10, 1, 2]
```

In general, heap pointers can be used as stack pointers if necessary, since
the usage of stack pointers is restricted, but heap pointers don't have the
same restrictions, so it's good practice to define functions that don't need
to store pointers to use stack references. This lets you pass references to
local variables or pointers to heap data depending on your needs.

```tomo
func swap_first_two(data:&[Int]):
    data[1], data[2] = data[2], data[1]

...

heap_nums := @[10, 20, 30]
swap_first_two(heap_nums)

local_nums := [10, 20, 30]
swap_first_two(&local_nums)
```

## Dereferencing

Pointers can be dereferenced to access the value that's stored at the pointer's
memory location using the `[]` postfix operator (with no value inside).

```tomo
nums := @[10, 20]
>> nums[]
= [10, 20]
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
>> x == y
= no

z := x
>> x == z
= yes
```

Pointers are ordered by memory address, which is somewhat arbitrary, but
consistent.

## Null Safety

Tomo pointers are, by default, guaranteed to be non-null. If you write a
function that takes either a `&T` or `@T`, the value that will be given
is always non-null. However, optional pointers can be used by adding a
question mark to the type: `&T?` or `@T?`. A null value can be created
using the syntax `!@T` or `!&T`. You can also append a question mark to
a pointer value so the type checker knows it's supposed to be optional:

```
optional := @[10, 20]?
optional := &foo?
```

The compiler will not allow you to dereference an optionally null pointer
without explicitly checking for null. To do so, use a conditional check like
this, and everywhere inside the truthy block will allow you to use the pointer
as a non-null pointer:

```
if optional:
    ok := optional[]
else:
    say("Oh, it was null")
```

## Using Pointers

For convenience, most operations that work on values can work with pointers to
values implicitly. For example, if you have a struct type with a `.foo` field,
you can use `ptr.foo` on a pointer to that struct type as well, without needing
to use `ptr[].foo`. The same is true for array accesses like `ptr[i]` and method
calls like `ptr:reversed()`.

As a matter of convenience, local variables can also be automatically promoted
to stack references when invoking methods that require a stack reference as the
first argument. For example:

```tomo
func swap_first_two(arr:&[Int]):
    arr[1], arr[2] = arr[2], arr[1]
...
my_arr := [10, 20, 30] // not a pointer
swap_first_two(my_arr) // ok, automatically converted to &my_arr
my_arr:shuffle() // ok, automatically converted to &my_arr
```
