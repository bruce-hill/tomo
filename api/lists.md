% API

# Builtins

# List
## List.binary_search

```tomo
List.binary_search : func(list: [T], by: func(x,y:&T->Int32) = T.compare -> Int)
```

Performs a binary search on a sorted list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The sorted list to search.  | -
by | `func(x,y:&T->Int32)` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Assuming the input list is sorted according to the given comparison function, return the index where the given item would be inserted to maintain the sorted order. That is, if the item is found, return its index, otherwise return the place where it would be found if it were inserted and the list were sorted.


**Example:**
```tomo
assert [1, 3, 5, 7, 9].binary_search(5) == 3
assert [1, 3, 5, 7, 9].binary_search(-999) == 1
assert [1, 3, 5, 7, 9].binary_search(999) == 6

```
## List.by

```tomo
List.by : func(list: [T], step: Int -> [T])
```

Creates a new list with elements spaced by the specified step value.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
step | `Int` | The step value for selecting elements.  | -

**Return:** A new list with every `step`-th element from the original list.


**Example:**
```tomo
assert [1, 2, 3, 4, 5, 6].by(2) == [1, 3, 5]

```
## List.clear

```tomo
List.clear : func(list: @[T] -> Void)
```

Clears all elements from the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be cleared.  | -

**Return:** Nothing.


**Example:**
```tomo
my_list.clear()

```
## List.counts

```tomo
List.counts : func(list: [T] -> {T=Int})
```

Counts the occurrences of each element in the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to count elements in.  | -

**Return:** A table mapping each element to its count.


**Example:**
```tomo
assert [10, 20, 30, 30, 30].counts() == {10=1, 20=1, 30=3}

```
## List.find

```tomo
List.find : func(list: [T], target: T -> Int?)
```

Finds the index of the first occurrence of an element (if any).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to search through.  | -
target | `T` | The item to search for.  | -

**Return:** The index of the first occurrence or `none` if not found.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].find(20) == 2
assert [10, 20, 30, 40, 50].find(9999) == none

```
## List.from

```tomo
List.from : func(list: [T], first: Int -> [T])
```

Returns a slice of the list starting from a specified index.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
first | `Int` | The index to start from.  | -

**Return:** A new list starting from the specified index.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].from(3) == [30, 40, 50]

```
## List.has

```tomo
List.has : func(list: [T], target: T -> Bool)
```

Checks if the list has an element.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to check.  | -
target | `T` | The element to check for.  | -

**Return:** `yes` if the list has the element, `no` otherwise.


**Example:**
```tomo
assert [10, 20, 30].has(20) == yes

```
## List.heap_pop

```tomo
List.heap_pop : func(list: @[T], by: func(x,y:&T->Int32) = T.compare -> T?)
```

Removes and returns the top element of a heap or `none` if the list is empty. By default, this is the *minimum* value in the heap.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the heap.  | -
by | `func(x,y:&T->Int32)` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** The removed top element of the heap or `none` if the list is empty.


**Example:**
```tomo
my_heap := [30, 10, 20]
my_heap.heapify()
assert my_heap.heap_pop() == 10

```
## List.heap_push

```tomo
List.heap_push : func(list: @[T], item: T, by = T.compare -> Void)
```

Adds an element to the heap and maintains the heap property. By default, this is a *minimum* heap.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the heap.  | -
item | `T` | The item to be added.  | -
by | `` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Nothing.


**Example:**
```tomo
my_heap.heap_push(10)

```
## List.heapify

```tomo
List.heapify : func(list: @[T], by: func(x,y:&T->Int32) = T.compare -> Void)
```

Converts a list into a heap.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be heapified.  | -
by | `func(x,y:&T->Int32)` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Nothing.


**Example:**
```tomo
my_heap := [30, 10, 20]
my_heap.heapify()

```
## List.insert

```tomo
List.insert : func(list: @[T], item: T, at: Int = 0 -> Void)
```

Inserts an element at a specified position in the list.

Since indices are 1-indexed and negative indices mean "starting from the back", an index of `0` means "after the last item".

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
item | `T` | The item to be inserted.  | -
at | `Int` | The index at which to insert the item.  | `0`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20]
list.insert(30)
assert list == [10, 20, 30]

list.insert(999, at=2)
assert list == [10, 999, 20, 30]

```
## List.insert_all

```tomo
List.insert_all : func(list: @[T], items: [T], at: Int = 0 -> Void)
```

Inserts a list of items at a specified position in the list.

Since indices are 1-indexed and negative indices mean "starting from the back", an index of `0` means "after the last item".

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
items | `[T]` | The items to be inserted.  | -
at | `Int` | The index at which to insert the item.  | `0`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20]
list.insert_all([30, 40])
assert list == [10, 20, 30, 40]

list.insert_all([99, 100], at=2)
assert list == [10, 99, 100, 20, 30, 40]

```
## List.pop

```tomo
List.pop : func(list: &[T], index: Int = -1 -> T?)
```

Removes and returns an item from the list. If the given index is present in the list, the item at that index will be removed and the list will become one element shorter.

Since negative indices are counted from the back, the default behavior is to pop the last value.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `&[T]` | The list to remove an item from.  | -
index | `Int` | The index from which to remove the item.  | `-1`

**Return:** `none` if the list is empty or the given index does not exist in the list, otherwise the item at the given index.


**Example:**
```tomo
list := &[10, 20, 30, 40]

assert list.pop() == 40
assert list[] == [10, 20, 30]

assert list.pop(index=2) == 20
assert list[] == [10, 30]

```
## List.random

```tomo
List.random : func(list: [T], random: func(min,max:Int64->Int64)? = none -> T)
```

Selects a random element from the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list from which to select a random element.  | -
random | `func(min,max:Int64->Int64)?` | If provided, this function will be used to get a random index in the list. Returned values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** A random element from the list.


**Example:**
```tomo
assert [10, 20, 30].random() == 20

```
## List.remove_at

```tomo
List.remove_at : func(list: @[T], at: Int = -1, count: Int = 1 -> Void)
```

Removes elements from the list starting at a specified index.

Since negative indices are counted from the back, the default behavior is to remove the last item.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
at | `Int` | The index at which to start removing elements.  | `-1`
count | `Int` | The number of elements to remove.  | `1`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20, 30, 40, 50]
list.remove_at(2)
assert list == [10, 30, 40, 50]

list.remove_at(2, count=2)
assert list == [10, 50]

```
## List.remove_item

```tomo
List.remove_item : func(list: @[T], item: T, max_count: Int = -1 -> Void)
```

Removes all occurrences of a specified item from the list.

A negative `max_count` means "remove all occurrences".

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list.  | -
item | `T` | The item to be removed.  | -
max_count | `Int` | The maximum number of occurrences to remove.  | `-1`

**Return:** Nothing.


**Example:**
```tomo
list := [10, 20, 10, 20, 30]
list.remove_item(10)
assert list == [20, 20, 30]

list.remove_item(20, max_count=1)
assert list == [20, 30]

```
## List.reversed

```tomo
List.reversed : func(list: [T] -> [T])
```

Returns a reversed slice of the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to be reversed.  | -

**Return:** A slice of the list with elements in reverse order.


**Example:**
```tomo
assert [10, 20, 30].reversed() == [30, 20, 10]

```
## List.sample

```tomo
List.sample : func(list: [T], count: Int, weights: [Float64]? = none, random: func(->Float64)? = none -> [T])
```

Selects a sample of elements from the list, optionally with weighted probabilities.

Errors will be raised if any of the following conditions occurs: - The given list has no elements and `count >= 1` - `count < 0` (negative count) - The number of weights provided doesn't match the length of the list.  - Any weight in the weights list is negative, infinite, or `NaN` - The sum of the given weights is zero (zero probability for every element).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to sample from.  | -
count | `Int` | The number of elements to sample.  | -
weights | `[Float64]?` | The probability weights for each element in the list. These values do not need to add up to any particular number, they are relative weights. If no weights are given, elements will be sampled with uniform probability.  | `none`
random | `func(->Float64)?` | If provided, this function will be used to get random values for sampling the list. The provided function should return random numbers between `0.0` (inclusive) and `1.0` (exclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** A list of sampled elements from the list.


**Example:**
```tomo
assert [10, 20, 30].sample(2, weights=[90%, 5%, 5%]) == [10, 10]

```
## List.shuffle

```tomo
List.shuffle : func(list: @[T], random: func(min,max:Int64->Int64)? = none -> Void)
```

Shuffles the elements of the list in place.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be shuffled.  | -
random | `func(min,max:Int64->Int64)?` | If provided, this function will be used to get a random index in the list. Returned values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** Nothing.


**Example:**
```tomo
list.shuffle()

```
## List.shuffled

```tomo
List.shuffled : func(list: [T], random: func(min,max:Int64->Int64)? = none -> [T])
```

Creates a new list with elements shuffled.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to be shuffled.  | -
random | `func(min,max:Int64->Int64)?` | If provided, this function will be used to get a random index in the list. Returned values must be between `min` and `max` (inclusive). (Used for deterministic pseudorandom number generation)  | `none`

**Return:** A new list with shuffled elements.


**Example:**
```tomo
assert [10, 20, 30, 40].shuffled() == [40, 10, 30, 20]

```
## List.slice

```tomo
List.slice : func(list: [T], from: Int, to: Int -> [T])
```

Returns a slice of the list spanning the given indices (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
from | `Int` | The first index to include.  | -
to | `Int` | The last index to include.  | -

**Return:** A new list spanning the given indices. Note: negative indices are counted from the back of the list, so `-1` refers to the last element, `-2` the second-to-last, and so on.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].slice(2, 4) == [20, 30, 40]
assert [10, 20, 30, 40, 50].slice(-3, -2) == [30, 40]

```
## List.sort

```tomo
List.sort : func(list: @[T], by = T.compare -> Void)
```

Sorts the elements of the list in place in ascending order (small to large).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `@[T]` | The mutable reference to the list to be sorted.  | -
by | `` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** Nothing.


**Example:**
```tomo
list := [40, 10, -30, 20]
list.sort()
assert list == [-30, 10, 20, 40]

list.sort(func(a,b:&Int): a.abs() <> b.abs())
assert list == [10, 20, -30, 40]

```
## List.sorted

```tomo
List.sorted : func(list: [T], by = T.compare -> [T])
```

Creates a new list with elements sorted.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to be sorted.  | -
by | `` | The comparison function used to determine order. If not specified, the default comparison function for the item type will be used.  | `T.compare`

**Return:** A new list with sorted elements.


**Example:**
```tomo
assert [40, 10, -30, 20].sorted() == [-30, 10, 20, 40]
assert [40, 10, -30, 20].sorted(
   func(a,b:&Int): a.abs() <> b.abs()
) == [10, 20, -30, 40]

```
## List.to

```tomo
List.to : func(list: [T], last: Int -> [T])
```

Returns a slice of the list from the start of the original list up to a specified index (inclusive).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The original list.  | -
last | `Int` | The index up to which elements should be included.  | -

**Return:** A new list containing elements from the start up to the specified index.


**Example:**
```tomo
assert [10, 20, 30, 40, 50].to(3) == [10, 20, 30]
assert [10, 20, 30, 40, 50].to(-2) == [10, 20, 30, 40]

```
## List.unique

```tomo
List.unique : func(list: [T] -> {T})
```

Returns a set of the unique elements of the list.

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to process.  | -

**Return:** A set of the unique elements from the list.


**Example:**
```tomo
assert [10, 20, 10, 10, 30].unique() == {10, 20, 30}

```
## List.where

```tomo
List.where : func(list: [T], predicate: func(item:&T -> Bool) -> Int)
```

Find the index of the first item that matches a predicate function (if any).

Argument | Type | Description | Default
---------|------|-------------|---------
list | `[T]` | The list to search through.  | -
predicate | `func(item:&T -> Bool)` | A function that returns `yes` if the item's index should be returned or `no` if it should not.  | -

**Return:** Returns the index of the first item where the predicate is true or `none` if no item matches.


**Example:**
```tomo
assert [4, 5, 6].where(func(i:&Int): i.is_prime()) == 5
assert [4, 6, 8].find(func(i:&Int): i.is_prime()) == none

```
