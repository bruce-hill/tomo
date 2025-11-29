# Lists

Tomo supports lists as a container type that holds a list of elements of any
type in a compact format similar to a C-style array. Lists are immutable by
default, but use copy-on-write semantics to efficiently mutate in place when
possible. **Lists are 1-indexed**, which means the first item in the list has
index `1`.

## Syntax

Lists are written using square brackets and a list of comma-separated elements:

```tomo
nums := [10, 20, 30]
```

Each element must have the same type (or be easily promoted to the same type). If
you want to have an empty list, you must specify what type goes inside the list
like this:

```tomo
empty : [Int] = []
```

For type annotations, a list that holds items with type `T` is written as `[T]`.

### List Comprehensions

Lists can also use comprehensions, where you specify how to dynamically create
all the elements by iteration instead of manually specifying each:

```tomo
assert [i*10 for i in (3).to(8)] == [30, 40, 50, 60, 70, 80]
assert [i*10 for i in (3).to(8) if i != 4] == [30, 50, 60, 70, 80]
```

Comprehensions can be combined with regular items or other comprehensions:

```tomo
nums := [-1, i*10 for i in (3).to(8), i for i in 3]
assert nums == [-1, 30, 40, 50, 60, 70, 80, 1, 2, 3]
```

## Length

List length can be accessed by the `.length` field:

```tomo
assert [10, 20, 30].length == 3
```

## Indexing

List values are accessed using square bracket indexing. Since lists are
1-indexed, the index `1` corresponds to the first item in the list. Negative
indices are used to refer to items from the back of the list, so `-1` is the
last item, `-2` is the second-to-last, and so on.

```tomo
list := [10, 20, 30, 40]
assert list[1] == 10?

assert list[2] == 20?

assert list[999] == none

assert list[-1] == 40?

assert list[-2] == 30?
```

If a list index of `0` or any value larger than the length of the list is
used, a `none` value will be returned.

## Iteration

You can iterate over the items in a list like this:

```tomo
for item in list
    ...

for i, item in list
    ...
```

List iteration operates over the value of the list when the loop began, so
modifying the list during iteration is safe and will not result in the loop
iterating over any of the new values.

## Concatenation

Lists can be concatenated with the `++` operator, which returns a list that
has the items from one appended to the other. This should not be confused with
the addition operator `+`, which does not work with lists.

```tomo
assert [1, 2] ++ [3, 4] == [1, 2, 3, 4]
```

## Implementation Details 

Under the hood, lists are implemented as a struct that contains a pointer to a
contiguous chunk of memory storing the elements of the list and some other
metadata. Since Tomo has datatypes with different sizes, like `Bool`s which
take one byte and `struct`s which can take up many bytes, it's worth noting
that lists store the elements compactly and inline, without the need for each
list cell to hold a pointer to where the data actually lives.

The other metadata stored with a list includes its length as well as the
_stride_ of the list. The stride is not exposed to the user, but it's the gap
in bytes between each element in the list. The reason this is mentioned is
that it is possible to create immutable slices of lists in constant time by
creating a new struct that points to the appropriate starting place for the
list items and has the appropriate stride. The upshot is that a method like
`list.reversed()` does not actually copy the list, it simply returns a struct
that points to the back of the list with a negative stride. Lists adhere to
copy-on-write semantics, so we can cheaply create many read-only references to
the same data, and only need to do copying if we plan to modify data. After
doing a modification, future modifications can be done in-place as long as
there is only one reference to that data.

Internally, we also take advantage of this inside of tables, which compactly
store all of the key/value pairs in a contiguous list and we can return an
immutable slice of that list showing only the keys or only the values by
choosing the right starting point and stride.

## Copy on Write

Lists can be thought of as values that have copy-on-write semantics that use
reference counting to perform efficient in-place mutations instead of copying
as a performance optimization when it wouldn't affect the program's semantics.
Without getting too deep into the details, suffice it to say that when you
create a list, that list can be thought of as a singular "value" in the same
way that `123` is a value. That variable's value will never change unless you
explicitly perform an assignment operation on the variable or call a method on
the variable.

Because it would be tedious to require users to write all list operations as
pure functions like `list = list.with_value_at_index(value=x, index=i)`, Tomo
provides the familiar imperative syntax for modifying lists, but keeps the
semantics of the pure functional style. Writing `list[i] = x` is
_semantically_ equivalent to `list = list.with_value_at_index(value=x,
index=i)`, but much more readable and easy to write. Similarly,
`list.insert(x)` is semantically equivalent to `list =
list.with_value_inserted(x)`. We implement these mutating methods as functions
that take a pointer to a list variable, which then either mutate the list's
data in-place (if this is the only thing referencing that data) or construct a
new list and store its value in the memory where the list variable is stored.

When there is only a single reference to a list value, we can perform these
modifications in-place (lists typically have a little bit of spare capacity at
the end, so appending usually doesn't trigger a reallocation). When there are
shared references, we must create a copy of the list's data before modifying
it so the other references don't see the effects of the mutation. Here are some
simple examples:

```tomo
nums := [10, 20, 30, 39]

// Efficient in-place mutation because data references are not shared:
nums[4] = 40

// Constant time operation, but increments the reference count:
tmp := nums
assert tmp == [10, 20, 30, 40]

// Now, a mutation will trigger a copy-on-write,
// which resets the reference count to zero:
nums[4] = 999
assert nums == [10, 20, 30, 999]

// Because of the copy-on-write, `tmp` is unchanged:
assert tmp == [10, 20, 30, 40]

// Since the reference count has been reset, we can do more
// mutations without triggering another copy-on-write:
nums[4] = -1
assert nums == [10, 20, 30, -1]
```

List reference counting is _approximate_, but will only ever err on the side
of correctness at the expense of performance, not the other way around.
Occasionally, unnecessary copying may occur, but you should never experience an
list value changing because of some operation performed on a different list
value.

## List Pointers

Since the normal case of lists is to treat them like immutable values, what do
we do if we actually want to have a shared reference to a list whose contents
change over time? In that case, we want to use the `@` operator to create a
pointer to a heap-allocated list and pass that pointer around. This is the same
behavior that you get in Python when you create a `list`:

```tomo
nums := @[10, 20, 30]
tmp := nums

nums.insert(40)
assert tmp == @[10, 20, 30, 40]
```

Having multiple pointers to the same heap-allocated list does not cause the
list's reference count to increase, because there is only one "value" in play:
the one stored on the heap. It's only when we store the "value" in multiple
places that we need to increment the reference count:

```tomo
// Increment the reference count, because `value` now has to hold
// whatever data was at the pointer's location at this point in time: 
value := nums[]
```

The TL;DR is: you can cheaply modify local variables that aren't aliased or
`@`-allocated lists, but if you assign a local variable list to another
variable or dereference a heap pointer, it may trigger copy-on-write behavior.

# API

[API documentation](../api/lists.md)
