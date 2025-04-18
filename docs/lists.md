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
>> [i*10 for i in (3).to(8)]
= [30, 40, 50, 60, 70, 80]
>> [i*10 for i in (3).to(8) if i != 4]
= [30, 50, 60, 70, 80]
```

Comprehensions can be combined with regular items or other comprehensions:

```tomo
>> [-1, i*10 for i in (3).to(8), i for i in 3]
= [-1, 30, 40, 50, 60, 70, 80, 1, 2, 3]
```

## Length

List length can be accessed by the `.length` field:

```tomo
>> [10, 20, 30].length
= 3
```

## Indexing

List values are accessed using square bracket indexing. Since lists are
1-indexed, the index `1` corresponds to the first item in the list. Negative
indices are used to refer to items from the back of the list, so `-1` is the
last item, `-2` is the second-to-last, and so on.

```tomo
list := [10, 20, 30, 40]
>> list[1]
= 10

>> list[2]
= 20

>> list[-1]
= 40

>> list[-2]
= 30
```

If a list index of `0` or any value larger than the length of the list is
used, it will trigger a runtime error that will print what the invalid list
index was, the length of the list, and a stack trace. As a performance
operation, if list bounds checking proves to be a performance hot spot, you
can explicitly disable bounds checking by adding `list[i; unchecked]` to the
list access.

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
>> [1, 2] ++ [3, 4]
= [1, 2, 3, 4]
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
>> tmp
= [10, 20, 30, 40]

// Now, a mutation will trigger a copy-on-write,
// which resets the reference count to zero:
nums[4] = 999
>> nums
= [10, 20, 30, 999]

// Because of the copy-on-write, `tmp` is unchanged:
>> tmp
= [10, 20, 30, 40]

// Since the reference count has been reset, we can do more
// mutations without triggering another copy-on-write:
nums[4] = -1
>> nums
= [10, 20, 30, -1]
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
>> tmp
= @[10, 20, 30, 40]
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

## List Methods

- [`func binary_search(list: [T], by: func(x,y:&T->Int32) = T.compare -> Int)`](#binary_search)
- [`func by(list: [T], step: Int -> [T])`](#by)
- [`func clear(list: @[T] -> Void)`](#clear)
- [`func counts(list: [T] -> {T=Int})`](#counts)
- [`func find(list: [T], target: T -> Int?)`](#find)
- [`func first(list: [T], predicate: func(item:&T -> Bool) -> Int)`](#first)
- [`func from(list: [T], first: Int -> [T])`](#from)
- [`func has(list: [T], element: T -> Bool)`](#has)
- [`func heap_pop(list: @[T], by: func(x,y:&T->Int32) = T.compare -> T?)`](#heap_pop)
- [`func heap_push(list: @[T], item: T, by=T.compare -> Void)`](#heap_push)
- [`func heapify(list: @[T], by: func(x,y:&T->Int32) = T.compare -> Void)`](#heapify)
- [`func insert(list: @[T], item: T, at: Int = 0 -> Void)`](#insert)
- [`func insert_all(list: @[T], items: [T], at: Int = 0 -> Void)`](#insert_all)
- [`func pop(list: &[T], index: Int = -1 -> T?)`](#pop)
- [`func random(list: [T], random: func(min,max:Int64->Int64)? = none -> T)`](#random)
- [`func remove_at(list: @[T], at: Int = -1, count: Int = 1 -> Void)`](#remove_at)
- [`func remove_item(list: @[T], item: T, max_count: Int = -1 -> Void)`](#remove_item)
- [`func reversed(list: [T] -> [T])`](#reversed)
- [`func sample(list: [T], count: Int, weights: [Num]? = ![Num], random: func(->Num)? = none -> [T])`](#sample)
- [`func shuffle(list: @[T], random: func(min,max:Int64->Int64)? = none -> Void)`](#shuffle)
- [`func shuffled(list: [T], random: func(min,max:Int64->Int64)? = none -> [T])`](#shuffled)
- [`func slice(list: [T], from: Int, to: Int -> [T])`](#slice)
- [`func sort(list: @[T], by=T.compare -> Void)`](#sort)
- [`sorted(list: [T], by=T.compare -> [T])`](#sorted)
- [`to(list: [T], last: Int -> [T])`](#to)
- [`unique(list: [T] -> {T})`](#unique)

### `binary_search`
Performs a binary search on a sorted list.

```tomo
func binary_search(list: [T], by: func(x,y:&T->Int32) = T.compare -> Int)
```

- `list`: The sorted list to search.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Assuming the input list is sorted according to the given comparison function,
return the index where the given item would be inserted to maintain the sorted
order. That is, if the item is found, return its index, otherwise return the
place where it would be found if it were inserted and the list were sorted.

**Example:**  
```tomo
>> [1, 3, 5, 7, 9].binary_search(5)
= 3

>> [1, 3, 5, 7, 9].binary_search(-999)
= 1

>> [1, 3, 5, 7, 9].binary_search(999)
= 6
```

---

### `by`
Creates a new list with elements spaced by the specified step value.

```tomo
func by(list: [T], step: Int -> [T])
```

- `list`: The original list.
- `step`: The step value for selecting elements.

**Returns:**  
A new list with every `step`-th element from the original list.

**Example:**  
```tomo
>> [1, 2, 3, 4, 5, 6].by(2)
= [1, 3, 5]
```

---

### `clear`
Clears all elements from the list.

```tomo
func clear(list: @[T] -> Void)
```

- `list`: The mutable reference to the list to be cleared.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> my_list.clear()
```

---

### `counts`
Counts the occurrences of each element in the list.

```tomo
func counts(list: [T] -> {T=Int})
```

- `list`: The list to count elements in.

**Returns:**  
A table mapping each element to its count.

**Example:**  
```tomo
>> [10, 20, 30, 30, 30].counts()
= {10=1, 20=1, 30=3}
```

---

### `find`
Finds the index of the first occurrence of an element (if any).

```tomo
func find(list: [T], target: T -> Int?)
```

- `list`: The list to search through.
- `item`: The item to find in the list.

**Returns:**  
The index of the first occurrence or `!Int` if not found.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50].find(20)
= 2 : Int?

>> [10, 20, 30, 40, 50].find(9999)
= none : Int?
```

---

### `first`
Find the index of the first item that matches a predicate function (if any).

```tomo
func first(list: [T], predicate: func(item:&T -> Bool) -> Int)
```

- `list`: The list to search through.
- `predicate`: A function that returns `yes` if the item should be returned or
  `no` if it should not.

**Returns:**  
Returns the index of the first item where the predicate is true or `!Int` if no
item matches.

**Example:**  
```tomo
>> [4, 5, 6].find(func(i:&Int): i.is_prime())
= 5 : Int?
>> [4, 6, 8].find(func(i:&Int): i.is_prime())
= none : Int?
```

---

### `from`
Returns a slice of the list starting from a specified index.

```tomo
func from(list: [T], first: Int -> [T])
```

- `list`: The original list.
- `first`: The index to start from.

**Returns:**  
A new list starting from the specified index.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50].from(3)
= [30, 40, 50]
```

---

### `has`
Checks if the list has an element.

```tomo
func has(list: [T], element: T -> Bool)
```

- `list`: The list to check.
- `target`: The element to check for.

**Returns:**  
`yes` if the list has the target, `no` otherwise.

**Example:**  
```tomo
>> [10, 20, 30].has(20)
= yes
```

---

### `heap_pop`
Removes and returns the top element of a heap or `none` if the list is empty.
By default, this is the *minimum* value in the heap.

```tomo
func heap_pop(list: @[T], by: func(x,y:&T->Int32) = T.compare -> T?)
```

- `list`: The mutable reference to the heap.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
The removed top element of the heap or `none` if the list is empty.

**Example:**  
```tomo
>> my_heap := [30, 10, 20]
>> my_heap.heapify()
>> my_heap.heap_pop()
= 10
```

---

### `heap_push`
Adds an element to the heap and maintains the heap property. By default, this
is a *minimum* heap.

```tomo
func heap_push(list: @[T], item: T, by=T.compare -> Void)
```

- `list`: The mutable reference to the heap.
- `item`: The item to be added.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> my_heap.heap_push(10)
```

---

### `heapify`
Converts a list into a heap.

```tomo
func heapify(list: @[T], by: func(x,y:&T->Int32) = T.compare -> Void)
```

- `list`: The mutable reference to the list to be heapified.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> my_heap := [30, 10, 20]
>> my_heap.heapify()
```

---

### `insert`
Inserts an element at a specified position in the list.

```tomo
func insert(list: @[T], item: T, at: Int = 0 -> Void)
```

- `list`: The mutable reference to the list.
- `item`: The item to be inserted.
- `at`: The index at which to insert the item (default is `0`). Since indices
  are 1-indexed and negative indices mean "starting from the back", an index of
  `0` means "after the last item".

**Returns:**  
Nothing.

**Example:**  
```tomo
>> list := [10, 20]
>> list.insert(30)
>> list
= [10, 20, 30]

>> list.insert(999, at=2)
>> list
= [10, 999, 20, 30]
```

---

### `insert_all`
Inserts a list of items at a specified position in the list.

```tomo
func insert_all(list: @[T], items: [T], at: Int = 0 -> Void)
```

- `list`: The mutable reference to the list.
- `items`: The items to be inserted.
- `at`: The index at which to insert the item (default is `0`). Since indices
  are 1-indexed and negative indices mean "starting from the back", an index of
  `0` means "after the last item".

**Returns:**  
Nothing.

**Example:**  
```tomo
list := [10, 20]
list.insert_all([30, 40])
>> list
= [10, 20, 30, 40]

list.insert_all([99, 100], at=2)
>> list
= [10, 99, 100, 20, 30, 40]
```

---

### `pop`
Removes and returns an item from the list. If the given index is present in
the list, the item at that index will be removed and the list will become one
element shorter.

```tomo
func pop(list: &[T], index: Int = -1 -> T?)
```

- `list`: The list to remove an item from.
- `index`: The index from which to remove the item (default: the last item).

**Returns:**  
`none` if the list is empty or the given index does not exist in the list,
otherwise the item at the given index.

**Example:**  
```tomo
>> list := [10, 20, 30, 40]

>> list.pop()
= 40
>> list
= &[10, 20, 30]

>> list.pop(index=2)
= 20
>> list
= &[10, 30]
```

---

### `random`
Selects a random element from the list.

```tomo
func random(list: [T], random: func(min,max:Int64->Int64)? = none -> T)
```

- `list`: The list from which to select a random element.
- `random`: If provided, this function will be used to get a random index in the list. Returned
  values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number
  generation)

**Returns:**  
A random element from the list.

**Example:**  
```tomo
>> [10, 20, 30].random()
= 20
```

---

### `remove_at`
Removes elements from the list starting at a specified index.

```tomo
func remove_at(list: @[T], at: Int = -1, count: Int = 1 -> Void)
```

- `list`: The mutable reference to the list.
- `at`: The index at which to start removing elements (default is `-1`, which means the end of the list).
- `count`: The number of elements to remove (default is `1`).

**Returns:**  
Nothing.

**Example:**  
```tomo
list := [10, 20, 30, 40, 50]
list.remove_at(2)
>> list
= [10, 30, 40, 50]

list.remove_at(2, count=2)
>> list
= [10, 50]
```

---

### `remove_item`
Removes all occurrences of a specified item from the list.

```tomo
func remove_item(list: @[T], item: T, max_count: Int = -1 -> Void)
```

- `list`: The mutable reference to the list.
- `item`: The item to be removed.
- `max_count`: The maximum number of occurrences to remove (default is `-1`, meaning all occurrences).

**Returns:**  
Nothing.

**Example:**  
```tomo
list := [10, 20, 10, 20, 30]
list.remove_item(10)
>> list
= [20, 20, 30]

list.remove_item(20, max_count=1)
>> list
= [20, 30]
```

---

### `reversed`
Returns a reversed slice of the list.

```tomo
func reversed(list: [T] -> [T])
```

- `list`: The list to be reversed.

**Returns:**  
A slice of the list with elements in reverse order.

**Example:**  
```tomo
>> [10, 20, 30].reversed()
= [30, 20, 10]
```

---

### `sample`
Selects a sample of elements from the list, optionally with weighted
probabilities.

```tomo
func sample(list: [T], count: Int, weights: [Num]? = ![Num], random: func(->Num)? = none -> [T])
```

- `list`: The list to sample from.
- `count`: The number of elements to sample.
- `weights`: The probability weights for each element in the list. These
  values do not need to add up to any particular number, they are relative
  weights. If no weights are given, elements will be sampled with uniform
  probability.
- `random`: If provided, this function will be used to get random values for
  sampling the list. The provided function should return random numbers
  between `0.0` (inclusive) and `1.0` (exclusive). (Used for deterministic
  pseudorandom number generation)

**Errors:**
Errors will be raised if any of the following conditions occurs:
- The given list has no elements and `count >= 1`
- `count < 0` (negative count)
- The number of weights provided doesn't match the length of the list. 
- Any weight in the weights list is negative, infinite, or `NaN`
- The sum of the given weights is zero (zero probability for every element).

**Returns:**  
A list of sampled elements from the list.

**Example:**  
```tomo
>> [10, 20, 30].sample(2, weights=[90%, 5%, 5%])
= [10, 10]
```

---

### `shuffle`
Shuffles the elements of the list in place.

```tomo
func shuffle(list: @[T], random: func(min,max:Int64->Int64)? = none -> Void)
```

- `list`: The mutable reference to the list to be shuffled.
- `random`: If provided, this function will be used to get a random index in the list. Returned
  values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number
  generation)

**Returns:**  
Nothing.

**Example:**  
```tomo
>> list.shuffle()
```

---

### `shuffled`
Creates a new list with elements shuffled.

```tomo
func shuffled(list: [T], random: func(min,max:Int64->Int64)? = none -> [T])
```

- `list`: The list to be shuffled.
- `random`: If provided, this function will be used to get a random index in the list. Returned
  values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number
  generation)

**Returns:**  
A new list with shuffled elements.

**Example:**  
```tomo
>> [10, 20, 30, 40].shuffled()
= [40, 10, 30, 20]
```

---

### `slice`
Returns a slice of the list spanning the given indices (inclusive).

```tomo
func slice(list: [T], from: Int, to: Int -> [T])
```

- `list`: The original list.
- `from`: The first index to include.
- `to`: The last index to include.

**Returns:**  
A new list spanning the given indices. Note: negative indices are counted from
the back of the list, so `-1` refers to the last element, `-2` the
second-to-last, and so on.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50].slice(2, 4)
= [20, 30, 40]

>> [10, 20, 30, 40, 50].slice(-3, -2)
= [30, 40]
```

---

### `sort`
Sorts the elements of the list in place in ascending order (small to large).

```tomo
func sort(list: @[T], by=T.compare -> Void)
```

- `list`: The mutable reference to the list to be sorted.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```tomo
list := [40, 10, -30, 20]
list.sort()
>> list
= [-30, 10, 20, 40]

list.sort(func(a,b:&Int): a.abs() <> b.abs())
>> list
= [10, 20, -30, 40]
```

---

### `sorted`
Creates a new list with elements sorted.

```tomo
func sorted(list: [T], by=T.compare -> [T])
```

- `list`: The list to be sorted.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
A new list with sorted elements.

**Example:**  
```tomo
>> [40, 10, -30, 20].sorted()
= [-30, 10, 20, 40]

>> [40, 10, -30, 20].sorted(func(a,b:&Int): a.abs() <> b.abs())
= [10, 20, -30, 40]
```

---

### `to`
Returns a slice of the list from the start of the original list up to a specified index (inclusive).

```tomo
func to(list: [T], last: Int -> [T])
```

- `list`: The original list.
- `last`: The index up to which elements should be included.

**Returns:**  
A new list containing elements from the start up to the specified index.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50].to(3)
= [10, 20, 30]

>> [10, 20, 30, 40, 50].to(-2)
= [10, 20, 30, 40]
```

---

### `unique`
Returns a Set that contains the unique elements of the list.

```tomo
func unique(list: [T] -> |T|)
```

- `list`: The list to process.

**Returns:**  
A set containing only unique elements from the list.

**Example:**  
```tomo
>> [10, 20, 10, 10, 30].unique()
= {10, 20, 30}
```
