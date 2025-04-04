# Sets

Sets represent an unordered collection of unique elements. These are
implemented using hash tables.

```tomo
a := {10, 20, 30}
b := {20, 30}
>> a:overlap(b)
= {20}
```

## Syntax

Sets are written using `{}` curly braces with comma-separated items:

```tomo
nums := {10, 20, 30}
```

Empty sets must specify the item type explicitly:

```tomo
empty := {:Int}
```

For type annotations, a set that holds items with type `T` is written as `{T}`.

### Comprehensions

Similar to arrays, sets can use comprehensions:

```tomo
set := {10*i for i in 10}
set2 := {10*i for i in 10 if i mod 2 == 0}
set3 := {-10, 10*i for i in 10}
```

## Accessing Items

Sets internally store their items in an array, which you can access with the
`.items` field. This is a constant-time operation that produces an immutable
view:

```tomo
set := {10, 20, 30}
>> set.items
= [10, 20, 30]
```

## Length

Set length can be accessed by the `.length` field:

```tomo
>> {10, 20, 30}.length
= 3
```

## Iteration

You can iterate over the items in a table like this:

```tomo
for item in set:
    ...

for i, item in set:
    ...
```

Set iteration operates over the value of the set when the loop began, so
modifying the set during iteration is safe and will not result in the loop
iterating over any of the new values.

## Set Methods

- [`func add(set:{T}, item: T -> Void)`](#add)
- [`func add_all(set:@{T}, items: [T] -> Void)`](#add_all)
- [`func clear(set:@{T} -> Void)`](#clear)
- [`func has(set:{T}, item:T -> Bool)`](#has)
- [`func (set: {T}, other: {T}, strict: Bool = no -> Bool)`](#is_subset_of)
- [`func is_superset_of(set:{T}, other: {T}, strict: Bool = no -> Bool)`](#is_superset_of)
- [`func overlap(set:{T}, other: {T} -> {T})`](#overlap)
- [`func remove(set:@{T}, item: T -> Void)`](#remove)
- [`func remove_all(set:@{T}, items: [T] -> Void)`](#remove_all)
- [`func with(set:{T}, other: {T} -> {T})`](#with)
- [`func without(set:{T}, other: {T} -> {T})`](#without)

### `add`
Adds an item to the set.

```tomo
func add(set:{T}, item: T -> Void)
```

- `set`: The mutable reference to the set.
- `item`: The item to add to the set.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> nums:add(42)
```

---

### `add_all`
Adds multiple items to the set.

```tomo
func add_all(set:@{T}, items: [T] -> Void)
```

- `set`: The mutable reference to the set.
- `items`: The array of items to add to the set.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> nums:add_all([1, 2, 3])
```

---

### `clear`
Removes all items from the set.

```tomo
func clear(set:@{T} -> Void)
```

- `set`: The mutable reference to the set.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> nums:clear()
```

---

### `has`
Checks if the set contains a specified item.

```tomo
func has(set:{T}, item:T -> Bool)
```

- `set`: The set to check.
- `item`: The item to check for presence.

**Returns:**  
`yes` if the item is present, `no` otherwise.

**Example:**  
```tomo
>> {10, 20}:has(20)
= yes
```

---

### `is_subset_of`
Checks if the set is a subset of another set.

```tomo
func (set: {T}, other: {T}, strict: Bool = no -> Bool)
```

- `set`: The set to check.
- `other`: The set to compare against.
- `strict`: If `yes`, checks if the set is a strict subset (does not equal the other set).

**Returns:**  
`yes` if the set is a subset of the other set (strictly or not), `no` otherwise.

**Example:**  
```tomo
>> {1, 2}:is_subset_of({1, 2, 3})
= yes
```

---

### `is_superset_of`
Checks if the set is a superset of another set.

```tomo
func is_superset_of(set:{T}, other: {T}, strict: Bool = no -> Bool)
```

- `set`: The set to check.
- `other`: The set to compare against.
- `strict`: If `yes`, checks if the set is a strict superset (does not equal the other set).

**Returns:**  
`yes` if the set is a superset of the other set (strictly or not), `no` otherwise.

**Example:**  
```tomo
>> {1, 2, 3}:is_superset_of({1, 2})
= yes
```
### `overlap`
Creates a new set with items that are in both the original set and another set.

```tomo
func overlap(set:{T}, other: {T} -> {T})
```

- `set`: The original set.
- `other`: The set to intersect with.

**Returns:**  
A new set containing only items present in both sets.

**Example:**  
```tomo
>> {1, 2}:overlap({2, 3})
= {2}
```

---

### `remove`
Removes an item from the set.

```tomo
func remove(set:@{T}, item: T -> Void)
```

- `set`: The mutable reference to the set.
- `item`: The item to remove from the set.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> nums:remove(42)
```

---

### `remove_all`
Removes multiple items from the set.

```tomo
func remove_all(set:@{T}, items: [T] -> Void)
```

- `set`: The mutable reference to the set.
- `items`: The array of items to remove from the set.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> nums:remove_all([1, 2, 3])
```

---

### `with`
Creates a new set that is the union of the original set and another set.

```tomo
func with(set:{T}, other: {T} -> {T})
```

- `set`: The original set.
- `other`: The set to union with.

**Returns:**  
A new set containing all items from both sets.

**Example:**  
```tomo
>> {1, 2}:with({2, 3})
= {1, 2, 3}
```

---

### `without`
Creates a new set with items from the original set but without items from another set.

```tomo
func without(set:{T}, other: {T} -> {T})
```

- `set`: The original set.
- `other`: The set of items to remove from the original set.

**Returns:**  
A new set containing items from the original set excluding those in the other set.

**Example:**  
```tomo
>> {1, 2}:without({2, 3})
= {1}
```
