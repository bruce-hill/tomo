# Arrays

Tomo supports arrays as a container type that holds a list of elements of any
type in a compact format. Arrays are immutable by default, but use
copy-on-write semantics to efficiently mutate in place when possible. **Arrays
are 1-indexed**, which means the first item in the array has index `1`.

## Syntax

Arrays are written using square brackets and a list of comma-separated elements:

```tomo
nums := [10, 20, 30]
```

Each element must have the same type (or be easily promoted to the same type). If
you want to have an empty array, you must specify what type goes inside the array
like this:

```tomo
empty := [:Int]
```

For type annotations, an array that holds items with type `T` is written as `[T]`.

### Array Comprehensions

Arrays can also use comprehensions, where you specify how to dynamically create
all the elements by iteration instead of manually specifying each:

```tomo
>> [i*10 for i in 3:to(8)]
= [30, 40, 50, 60, 70, 80]
>> [i*10 for i in 3:to(8) if i != 4]
= [30, 50, 60, 70, 80]
```

Comprehensions can be combined with regular items or other comprehensions:

```tomo
>> [-1, i*10 for i in 3:to(8), i for i in 3]
= [-1, 30, 40, 50, 60, 70, 80, 1, 2, 3]
```

## Length

Array length can be accessed by the `.length` field:

```tomo
>> [10, 20, 30].length
= 3
```

## Indexing

Array values are accessed using square bracket indexing. Since arrays are
1-indexed, the index `1` corresponds to the first item in the array. Negative
indices are used to refer to items from the back of the array, so `-1` is the
last item, `-2` is the second-to-last, and so on.

```tomo
arr := [10, 20, 30, 40]
>> arr[1]
= 10

>> arr[2]
= 20

>> arr[-1]
= 40

>> arr[-2]
= 30
```

If an array index of `0` or any value larger than the length of the array is
used, it will trigger a runtime error that will print what the invalid array
index was, the length of the array, and a stack trace. As a performance
operation, if array bounds checking proves to be a performance hot spot, you
can explicitly disable bounds checking by adding `arr[i; unchecked]` to the
array access.

## Iteration

You can iterate over the items in an array like this:

```tomo
for item in array:
    ...

for i, item in array:
    ...
```

Array iteration operates over the value of the array when the loop began, so
modifying the array during iteration is safe and will not result in the loop
iterating over any of the new values.

## Concatenation

Arrays can be concatenated with the `++` operator, which returns an array that
has the items from one appended to the other. This should not be confused with
the addition operator `+`, which does not work with arrays.

```tomo
>> [1, 2] ++ [3, 4]
= [1, 2, 3, 4]
```

## Implementation Details 

Under the hood, arrays are implemented as a struct that contains a pointer to a
contiguous chunk of memory storing the elements of the array and some other
metadata. Since Tomo has datatypes with different sizes, like `Bool`s which
take one byte and `struct`s which can take up many bytes, it's worth noting
that arrays store the elements compactly and inline, without the need for each
array cell to hold a pointer to where the data actually lives.

The other metadata stored with an array includes its length as well as the
_stride_ of the array. The stride is not exposed to the user, but it's the gap
in bytes between each element in the array. The reason this is mentioned is
that it is possible to create immutable slices of arrays in constant time by
creating a new struct that points to the appropriate starting place for the
array items and has the appropriate stride. The upshot is that a method like
`array:reversed()` does not actually copy the array, it simply returns a struct
that points to the back of the array with a negative stride. Arrays adhere to
copy-on-write semantics, so we can cheaply create many read-only references to
the same data, and only need to do copying if we plan to modify data. After
doing a modification, future modifications can be done in-place as long as
there is only one reference to that data.

Internally, we also take advantage of this inside of tables, which compactly
store all of the key/value pairs in a contiguous array and we can return an
immutable slice of that array showing only the keys or only the values by
choosing the right starting point and stride.

## Copy on Write

Arrays can be thought of as values that have copy-on-write semantics that use
reference counting to perform efficient in-place mutations instead of copying
as a performance optimization when it wouldn't affect the program's semantics.
Without getting too deep into the details, suffice it to say that when you
create an array, that array can be thought of as a singular "value" in the same
way that `123` is a value. That variable's value will never change unless you
explicitly perform an assignment operation on the variable or call a method on
the variable.

Because it would be tedious to require users to write all array operations as
pure functions like `array = array:with_value_at_index(value=x, index=i)`, Tomo
provides the familiar imperative syntax for modifying arrays, but keeps the
semantics of the pure functional style. Writing `array[i] = x` is
_semantically_ equivalent to `array = array:with_value_at_index(value=x,
index=i)`, but much more readable and easy to write. Similarly,
`array:insert(x)` is semantically equivalent to `array =
array:with_value_inserted(x)`. We implement these mutating methods as functions
that take a pointer to an array variable, which then either mutate the array's
data in-place (if this is the only thing referencing that data) or construct a
new array and store its value in the memory where the array variable is stored.

When there is only a single reference to an array value, we can perform these
modifications in-place (arrays typically have a little bit of spare capacity at
the end, so appending usually doesn't trigger a reallocation). When there are
shared references, we must create a copy of the array's data before modifying
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

Array reference counting is _approximate_, but will only ever err on the side
of correctness at the expense of performance, not the other way around.
Occasionally, unnecessary copying may occur, but you should never experience an
array value changing because of some operation performed on a different array
value.

## Array Pointers

Since the normal case of arrays is to treat them like immutable values, what do
we do if we actually want to have a shared reference to an array whose contents
change over time? In that case, we want to use the `@` operator to create a
pointer to a heap-allocated array and pass that pointer around. This is the same
behavior that you get in Python when you create a `list`:

```tomo
nums := @[10, 20, 30]
tmp := nums

nums:insert(40)
>> tmp
= @[10, 20, 30, 40]
```

Having multiple pointers to the same heap-allocated array does not cause the
array's reference count to increase, because there is only one "value" in play:
the one stored on the heap. It's only when we store the "value" in multiple
places that we need to increment the reference count:

```tomo
// Increment the reference count, because `value` now has to hold
// whatever data was at the pointer's location at this point in time: 
value := nums[]
```

The TL;DR is: you can cheaply modify local variables that aren't aliased or
`@`-allocated arrays, but if you assign a local variable array to another
variable or dereference a heap pointer, it may trigger copy-on-write behavior.

## Array Methods

- [`func binary_search(arr: [T], by: func(x,y:&T->Int32) = T.compare -> Int)`](#`binary_search)
- [`func by(arr: [T], step: Int -> [T])`](#`by)
- [`func clear(arr: @[T] -> Void)`](#`clear)
- [`func counts(arr: [T] -> {T,Int})`](#`counts)
- [`func find(arr: [T], target: T -> Int?)`](#`find)
- [`func first(arr: [T], predicate: func(item:&T -> Bool) -> Int)`](#`first)
- [`func from(arr: [T], first: Int -> [T])`](#`from)
- [`func has(arr: [T] -> Bool)`](#`has)
- [`func heap_pop(arr: @[T], by: func(x,y:&T->Int32) = T.compare -> T?)`](#`heap_pop)
- [`func heap_push(arr: @[T], item: T, by=T.compare -> Void)`](#`heap_push)
- [`func heapify(arr: @[T], by: func(x,y:&T->Int32) = T.compare -> Void)`](#`heapify)
- [`func insert(arr: @[T], item: T, at: Int = 0 -> Void)`](#`insert)
- [`func insert_all(arr: @[T], items: [T], at: Int = 0 -> Void)`](#`insert_all)
- [`func pop(arr: &[T], index: Int = -1 -> T?)`](#`pop)
- [`func random(arr: [T], rng: RNG = random -> T)`](#`random)
- [`func remove_at(arr: @[T], at: Int = -1, count: Int = 1 -> Void)`](#`remove_at)
- [`func remove_item(arr: @[T], item: T, max_count: Int = -1 -> Void)`](#`remove_item)
- [`func reversed(arr: [T] -> [T])`](#`reversed)
- [`func sample(arr: [T], count: Int, weights: [Num]? = ![Num], rng: RNG = random -> [T])`](#`sample)
- [`func shuffle(arr: @[T], rng: RNG = random -> Void)`](#`shuffle)
- [`func shuffled(arr: [T], rng: RNG = random -> [T])`](#`shuffled)
- [`func slice(arr: [T], from: Int, to: Int -> [T])`](#`slice)
- [`func sort(arr: @[T], by=T.compare -> Void)`](#`sort)
- [`sorted(arr: [T], by=T.compare -> [T])`](#`sorted)
- [`to(arr: [T], last: Int -> [T])`](#`to)
- [`unique(arr: [T] -> {T})`](#`unique)

### `binary_search`

**Description:**  
Performs a binary search on a sorted array.

**Signature:**  
```tomo
func binary_search(arr: [T], by: func(x,y:&T->Int32) = T.compare -> Int)
```

**Parameters:**

- `arr`: The sorted array to search.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Assuming the input array is sorted according to the given comparison function,
return the index where the given item would be inserted to maintain the sorted
order. That is, if the item is found, return its index, otherwise return the
place where it would be found if it were inserted and the array were sorted.

**Example:**  
```tomo
>> [1, 3, 5, 7, 9]:binary_search(5)
= 3

>> [1, 3, 5, 7, 9]:binary_search(-999)
= 1

>> [1, 3, 5, 7, 9]:binary_search(999)
= 6
```

---

### `by`

**Description:**  
Creates a new array with elements spaced by the specified step value.

**Signature:**  
```tomo
func by(arr: [T], step: Int -> [T])
```

**Parameters:**

- `arr`: The original array.
- `step`: The step value for selecting elements.

**Returns:**  
A new array with every `step`-th element from the original array.

**Example:**  
```tomo
>> [1, 2, 3, 4, 5, 6]:by(2)
= [1, 3, 5]
```

---

### `clear`

**Description:**  
Clears all elements from the array.

**Signature:**  
```tomo
func clear(arr: @[T] -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array to be cleared.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> my_array:clear()
```

---

### `counts`

**Description:**  
Counts the occurrences of each element in the array.

**Signature:**  
```tomo
func counts(arr: [T] -> {T,Int})
```

**Parameters:**

- `arr`: The array to count elements in.

**Returns:**  
A table mapping each element to its count.

**Example:**  
```tomo
>> [10, 20, 30, 30, 30]:counts()
= {10=1, 20=1, 30=3}
```

---

### `find`

**Description:**  
Finds the index of the first occurrence of an element (if any).

**Signature:**  
```tomo
func find(arr: [T], target: T -> Int?)
```

**Parameters:**

- `arr`: The array to search through.
- `item`: The item to find in the array.

**Returns:**  
The index of the first occurrence or `!Int` if not found.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50]:find(20)
= 2 : Int?

>> [10, 20, 30, 40, 50]:find(9999)
= none : Int?
```

---

### `first`

**Description:**  
Find the index of the first item that matches a predicate function (if any).

**Signature:**  
```tomo
func first(arr: [T], predicate: func(item:&T -> Bool) -> Int)
```

**Parameters:**

- `arr`: The array to search through.
- `predicate`: A function that returns `yes` if the item should be returned or
  `no` if it should not.

**Returns:**  
Returns the index of the first item where the predicate is true or `!Int` if no
item matches.

**Example:**  
```tomo
>> [4, 5, 6]:find(func(i:&Int): i:is_prime())
= 5 : Int?
>> [4, 6, 8]:find(func(i:&Int): i:is_prime())
= none : Int?
```

---

### `from`

**Description:**  
Returns a slice of the array starting from a specified index.

**Signature:**  
```tomo
func from(arr: [T], first: Int -> [T])
```

**Parameters:**

- `arr`: The original array.
- `first`: The index to start from.

**Returns:**  
A new array starting from the specified index.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50]:from(3)
= [30, 40, 50]
```

---

### `has`

**Description:**  
Checks if the array has any elements.

**Signature:**  
```tomo
func has(arr: [T] -> Bool)
```

**Parameters:**

- `arr`: The array to check.

**Returns:**  
`yes` if the array has elements, `no` otherwise.

**Example:**  
```tomo
>> [10, 20, 30]:has(20)
= yes
```

---

### `heap_pop`

**Description:**  
Removes and returns the top element of a heap or `none` if the array is empty.
By default, this is the *minimum* value in the heap.

**Signature:**  
```tomo
func heap_pop(arr: @[T], by: func(x,y:&T->Int32) = T.compare -> T?)
```

**Parameters:**

- `arr`: The mutable reference to the heap.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
The removed top element of the heap or `none` if the array is empty.

**Example:**  
```tomo
>> my_heap := [30, 10, 20]
>> my_heap:heapify()
>> my_heap:heap_pop()
= 10
```

---

### `heap_push`

**Description:**  
Adds an element to the heap and maintains the heap property. By default, this
is a *minimum* heap.

**Signature:**  
```tomo
func heap_push(arr: @[T], item: T, by=T.compare -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the heap.
- `item`: The item to be added.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> my_heap:heap_push(10)
```

---

### `heapify`

**Description:**  
Converts an array into a heap.

**Signature:**  
```tomo
func heapify(arr: @[T], by: func(x,y:&T->Int32) = T.compare -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array to be heapified.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> my_heap := [30, 10, 20]
>> my_heap:heapify()
```

---

### `insert`

**Description:**  
Inserts an element at a specified position in the array.

**Signature:**  
```tomo
func insert(arr: @[T], item: T, at: Int = 0 -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array.
- `item`: The item to be inserted.
- `at`: The index at which to insert the item (default is `0`). Since indices
  are 1-indexed and negative indices mean "starting from the back", an index of
  `0` means "after the last item".

**Returns:**  
Nothing.

**Example:**  
```tomo
>> arr := [10, 20]
>> arr:insert(30)
>> arr
= [10, 20, 30]

>> arr:insert(999, at=2)
>> arr
= [10, 999, 20, 30]
```

---

### `insert_all`

**Description:**  
Inserts an array of items at a specified position in the array.

**Signature:**  
```tomo
func insert_all(arr: @[T], items: [T], at: Int = 0 -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array.
- `items`: The items to be inserted.
- `at`: The index at which to insert the item (default is `0`). Since indices
  are 1-indexed and negative indices mean "starting from the back", an index of
  `0` means "after the last item".

**Returns:**  
Nothing.

**Example:**  
```tomo
arr := [10, 20]
arr:insert_all([30, 40])
>> arr
= [10, 20, 30, 40]

arr:insert_all([99, 100], at=2)
>> arr
= [10, 99, 100, 20, 30, 40]
```

---

### `pop`

**Description:**  
Removes and returns an item from the array. If the given index is present in
the array, the item at that index will be removed and the array will become one
element shorter.

**Signature:**  
```tomo
func pop(arr: &[T], index: Int = -1 -> T?)
```

**Parameters:**

- `arr`: The array to remove an item from.
- `index`: The index from which to remove the item (default: the last item).

**Returns:**  
`none` if the array is empty or the given index does not exist in the array,
otherwise the item at the given index.

**Example:**  
```tomo
>> arr := [10, 20, 30, 40]

>> arr:pop()
= 40
>> arr
= &[10, 20, 30]

>> arr:pop(index=2)
= 20
>> arr
= &[10, 30]
```

---

### `random`

**Description:**  
Selects a random element from the array.

**Signature:**  
```tomo
func random(arr: [T], rng: RNG = random -> T)
```

**Parameters:**

- `arr`: The array from which to select a random element.
- `rng`: The [random number generator](rng.md) to use.

**Returns:**  
A random element from the array.

**Example:**  
```tomo
>> [10, 20, 30]:random()
= 20
```

---

### `remove_at`

**Description:**  
Removes elements from the array starting at a specified index.

**Signature:**  
```tomo
func remove_at(arr: @[T], at: Int = -1, count: Int = 1 -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array.
- `at`: The index at which to start removing elements (default is `-1`, which means the end of the array).
- `count`: The number of elements to remove (default is `1`).

**Returns:**  
Nothing.

**Example:**  
```tomo
arr := [10, 20, 30, 40, 50]
arr:remove_at(2)
>> arr
= [10, 30, 40, 50]

arr:remove_at(2, count=2)
>> arr
= [10, 50]
```

---

### `remove_item`

**Description:**  
Removes all occurrences of a specified item from the array.

**Signature:**  
```tomo
func remove_item(arr: @[T], item: T, max_count: Int = -1 -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array.
- `item`: The item to be removed.
- `max_count`: The maximum number of occurrences to remove (default is `-1`, meaning all occurrences).

**Returns:**  
Nothing.

**Example:**  
```tomo
arr := [10, 20, 10, 20, 30]
arr:remove_item(10)
>> arr
= [20, 20, 30]

arr:remove_item(20, max_count=1)
>> arr
= [20, 30]
```

---

### `reversed`

**Description:**  
Returns a reversed slice of the array.

**Signature:**  
```tomo
func reversed(arr: [T] -> [T])
```

**Parameters:**

- `arr`: The array to be reversed.

**Returns:**  
A slice of the array with elements in reverse order.

**Example:**  
```tomo
>> [10, 20, 30]:reversed()
= [30, 20, 10]
```

---

### `sample`

**Description:**  
Selects a sample of elements from the array, optionally with weighted
probabilities.

**Signature:**  
```tomo
func sample(arr: [T], count: Int, weights: [Num]? = ![Num], rng: RNG = random -> [T])
```

**Parameters:**

- `arr`: The array to sample from.
- `count`: The number of elements to sample.
- `weights`: The probability weights for each element in the array. These
  values do not need to add up to any particular number, they are relative
  weights. If no weights are given, elements will be sampled with uniform
  probability.
- `rng`: The [random number generator](rng.md) to use.

**Errors:**
Errors will be raised if any of the following conditions occurs:
- The given array has no elements and `count >= 1`
- `count < 0` (negative count)
- The number of weights provided doesn't match the length of the array. 
- Any weight in the weights array is negative, infinite, or `NaN`
- The sum of the given weights is zero (zero probability for every element).

**Returns:**  
A list of sampled elements from the array.

**Example:**  
```tomo
>> [10, 20, 30]:sample(2, weights=[90%, 5%, 5%])
= [10, 10]
```

---

### `shuffle`

**Description:**  
Shuffles the elements of the array in place.

**Signature:**  
```tomo
func shuffle(arr: @[T], rng: RNG = random -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array to be shuffled.
- `rng`: The [random number generator](rng.md) to use.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> arr:shuffle()
```

---

### `shuffled`

**Description:**  
Creates a new array with elements shuffled.

**Signature:**  
```tomo
func shuffled(arr: [T], rng: RNG = random -> [T])
```

**Parameters:**

- `arr`: The array to be shuffled.
- `rng`: The [random number generator](rng.md) to use.

**Returns:**  
A new array with shuffled elements.

**Example:**  
```tomo
>> [10, 20, 30, 40]:shuffled()
= [40, 10, 30, 20]
```

---

### `slice`

**Description:**  
Returns a slice of the array spanning the given indices (inclusive).

**Signature:**  
```tomo
func slice(arr: [T], from: Int, to: Int -> [T])
```

**Parameters:**

- `arr`: The original array.
- `from`: The first index to include.
- `to`: The last index to include.

**Returns:**  
A new array spanning the given indices. Note: negative indices are counted from
the back of the array, so `-1` refers to the last element, `-2` the
second-to-last, and so on.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50]:slice(2, 4)
= [20, 30, 40]

>> [10, 20, 30, 40, 50]:slice(-3, -2)
= [30, 40]
```

---

### `sort`

**Description:**  
Sorts the elements of the array in place in ascending order (small to large).

**Signature:**  
```tomo
func sort(arr: @[T], by=T.compare -> Void)
```

**Parameters:**

- `arr`: The mutable reference to the array to be sorted.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```tomo
arr := [40, 10, -30, 20]
arr:sort()
>> arr
= [-30, 10, 20, 40]

arr:sort(func(a,b:&Int): a:abs() <> b:abs())
>> arr
= [10, 20, -30, 40]
```

---

### `sorted`

**Description:**  
Creates a new array with elements sorted.

**Signature:**  
```tomo
sorted(arr: [T], by=T.compare -> [T])
```

**Parameters:**

- `arr`: The array to be sorted.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
A new array with sorted elements.

**Example:**  
```tomo
>> [40, 10, -30, 20]:sorted()
= [-30, 10, 20, 40]

>> [40, 10, -30, 20]:sorted(func(a,b:&Int): a:abs() <> b:abs())
= [10, 20, -30, 40]
```

---

### `to`

**Description:**  
Returns a slice of the array from the start of the original array up to a specified index (inclusive).

**Signature:**  
```tomo
to(arr: [T], last: Int -> [T])
```

**Parameters:**

- `arr`: The original array.
- `last`: The index up to which elements should be included.

**Returns:**  
A new array containing elements from the start up to the specified index.

**Example:**  
```tomo
>> [10, 20, 30, 40, 50]:to(3)
= [10, 20, 30]

>> [10, 20, 30, 40, 50]:to(-2)
= [10, 20, 30, 40]
```

---

### `unique`

**Description:**  
Returns a Set that contains the unique elements of the array.

**Signature:**  
```tomo
unique(arr: [T] -> {T})
```

**Parameters:**

- `arr`: The array to process.

**Returns:**  
A set containing only unique elements from the array.

**Example:**  
```tomo
>> [10, 20, 10, 10, 30]:unique()
= {10, 20, 30}
```
