# Arrays

Tomo supports arrays as a container type that holds a list of elements of any
type in a compact format. Arrays are immutable by default, but use
copy-on-write semantics to efficiently mutate in place when possible. **Arrays
are 1-indexed**, which means the first item in the array has index `1`.

```tomo
nums := [10, 20, 30]
```

## Array Methods

### `binary_search`

**Description:**  
Performs a binary search on a sorted array.

**Usage:**  
```markdown
binary_search(arr: [T], by=T.compare) -> Int
```

**Parameters:**

- `arr`: The sorted array to search.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Assuming the input array is sorted according to the given comparison function,
return the index where the given item would be inserted to maintain the sorted
order.

**Example:**  
```markdown
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

**Usage:**  
```markdown
by(arr: [T], step: Int) -> [T]
```

**Parameters:**

- `arr`: The original array.
- `step`: The step value for selecting elements.

**Returns:**  
A new array with every `step`-th element from the original array.

**Example:**  
```markdown
>> [1, 2, 3, 4, 5, 6]:by(2)
= [1, 3, 5]
```

---

### `clear`

**Description:**  
Clears all elements from the array.

**Usage:**  
```markdown
clear(arr: & [T]) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the array to be cleared.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> my_array:clear()
```

---

### `counts`

**Description:**  
Counts the occurrences of each element in the array.

**Usage:**  
```markdown
counts(arr: [T]) -> {T: Int}
```

**Parameters:**

- `arr`: The array to count elements in.

**Returns:**  
A table mapping each element to its count.

**Example:**  
```markdown
>> [10, 20, 30, 30, 30]:counts()
= {10: 1, 20: 1, 30: 3}
```

---

### `find`

**Description:**  
Finds the index of the first occurrence of an element.

**Usage:**  
```markdown
find(arr: [T]) -> Int
```

**Parameters:**

- `arr`: The array to search through.

**Returns:**  
The index of the first occurrence or `-1` if not found.

**Example:**  
```markdown
>> [10, 20, 30, 40, 50]:find(20)
= 2

>> [10, 20, 30, 40, 50]:find(9999)
= -1
```

---

### `from`

**Description:**  
Returns a slice of the array starting from a specified index.

**Usage:**  
```markdown
from(arr: [T], first: Int) -> [T]
```

**Parameters:**

- `arr`: The original array.
- `first`: The index to start from.

**Returns:**  
A new array starting from the specified index.

**Example:**  
```markdown
>> [10, 20, 30, 40, 50]:from(3)
= [30, 40, 50]
```

---

### `has`

**Description:**  
Checks if the array has any elements.

**Usage:**  
```markdown
has(arr: [T]) -> Bool
```

**Parameters:**

- `arr`: The array to check.

**Returns:**  
`yes` if the array has elements, `no` otherwise.

**Example:**  
```markdown
>> [10, 20, 30]:has(20)
= yes
```

---

### `heap_pop`

**Description:**  
Removes and returns the top element of a heap. By default, this is the
*minimum* value in the heap.

**Usage:**  
```markdown
heap_pop(arr: & [T], by=T.compare) -> T
```

**Parameters:**

- `arr`: The mutable reference to the heap.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
The removed top element of the heap.

**Example:**  
```markdown
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

**Usage:**  
```markdown
heap_push(arr: & [T], item: T, by=T.compare) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the heap.
- `item`: The item to be added.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> my_heap:heap_push(10)
```

---

### `heapify`

**Description:**  
Converts an array into a heap.

**Usage:**  
```markdown
heapify(arr: & [T], by=T.compare) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the array to be heapified.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> my_heap := [30, 10, 20]
>> my_heap:heapify()
```

---

### `insert`

**Description:**  
Inserts an element at a specified position in the array.

**Usage:**  
```markdown
insert(arr: & [T], item: T, at: Int = 0) -> Void
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
```markdown
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

**Usage:**  
```markdown
insert_all(arr: & [T], items: [T], at: Int = 0) -> Void
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
```markdown
arr := [10, 20]
arr:insert_all([30, 40])
>> arr
= [10, 20, 30, 40]

arr:insert_all([99, 100], at=2)
>> arr
= [10, 99, 100, 20, 30, 40]
```

---

### `random`

**Description:**  
Selects a random element from the array.

**Usage:**  
```markdown
random(arr: [T]) -> T
```

**Parameters:**

- `arr`: The array from which to select a random element.

**Returns:**  
A random element from the array.

**Example:**  
```markdown
>> [10, 20, 30]:random()
= 20
```

---

### `remove_at`

**Description:**  
Removes elements from the array starting at a specified index.

**Usage:**  
```markdown
remove_at(arr: & [T], at: Int = -1, count: Int = 1) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the array.
- `at`: The index at which to start removing elements (default is `-1`, which means the end of the array).
- `count`: The number of elements to remove (default is `1`).

**Returns:**  
Nothing.

**Example:**  
```markdown
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

**Usage:**  
```markdown
remove_item(arr: & [T], item: T, max_count: Int = -1) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the array.
- `item`: The item to be removed.
- `max_count`: The maximum number of occurrences to remove (default is `-1`, meaning all occurrences).

**Returns:**  
Nothing.

**Example:**  
```markdown
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

**Usage:**  
```markdown
reversed(arr: [T]) -> [T]
```

**Parameters:**

- `arr`: The array to be reversed.

**Returns:**  
A slice of the array with elements in reverse order.

**Example:**  
```markdown
>> [10, 20, 30]:reversed()
= [30, 20, 10]
```

---

### `sample`

**Description:**  
Selects a sample of elements from the array, optionally with weighted
probabilities.

**Usage:**  
```markdown
sample(arr: [T], count: Int, weights: [Num]) -> [T]
```

**Parameters:**

- `arr`: The array to sample from.
- `count`: The number of elements to sample.
- `weights`: The probability weights for each element in the array. These
  values do not need to add up to any particular number, they are relative
  weights. If no weights are provided, the default is equal probabilities.
  Negative, infinite, or NaN weights will cause a runtime error. If the number of
  weights given is less than the length of the array, elements from the rest of
  the array are considered to have zero weight.

**Returns:**  
A list of sampled elements from the array.

**Example:**  
```markdown
>> [10, 20, 30]:sample(2, weights=[90%, 5%, 5%])
= [10, 10]
```

---

### `shuffle`

**Description:**  
Shuffles the elements of the array in place.

**Usage:**  
```markdown
shuffle(arr: & [T]) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the array to be shuffled.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> arr:shuffle()
```

---

### `shuffled`

**Description:**  
Creates a new array with elements shuffled.

**Usage:**  
```markdown
shuffled(arr: [T]) -> [T]
```

**Parameters:**

- `arr`: The array to be shuffled.

**Returns:**  
A new array with shuffled elements.

**Example:**  
```markdown
>> [10, 20, 30, 40]:shuffled()
= [40, 10, 30, 20]
```

---

### `sort`

**Description:**  
Sorts the elements of the array in place in ascending order (small to large).

**Usage:**  
```markdown
sort(arr: & [T], by=T.compare) -> Void
```

**Parameters:**

- `arr`: The mutable reference to the array to be sorted.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
Nothing.

**Example:**  
```markdown
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

**Usage:**  
```markdown
sorted(arr: [T], by=T.compare) -> [T]
```

**Parameters:**

- `arr`: The array to be sorted.
- `by`: The comparison function used to determine order. If not specified, the
  default comparison function for the item type will be used.

**Returns:**  
A new array with sorted elements.

**Example:**  
```markdown
>> [40, 10, -30, 20]:sorted()
= [-30, 10, 20, 40]

>> [40, 10, -30, 20]:sorted(func(a,b:&Int): a:abs() <> b:abs())
= [10, 20, -30, 40]
```

---

### `to`

**Description:**  
Returns a slice of the array from the start of the original array up to a specified index (inclusive).

**Usage:**  
```markdown
to(arr: [T], last: Int) -> [T]
```

**Parameters:**

- `arr`: The original array.
- `last`: The index up to which elements should be included.

**Returns:**  
A new array containing elements from the start up to the specified index.

**Example:**  
```markdown
>> [10, 20, 30, 40, 50]:to(3)
= [10, 20, 30]

>> [10, 20, 30, 40, 50]:to(-2)
= [10, 20, 30, 40]
```

---

### `unique`

**Description:**  
Returns a Set that contains the unique elements of the array.

**Usage:**  
```markdown
unique(arr: [T]) -> {T}
```

**Parameters:**

- `arr`: The array to process.

**Returns:**  
A set containing only unique elements from the array.

**Example:**  
```markdown
>> [10, 20, 10, 10, 30]:unique()
= {10, 20, 30}
```
