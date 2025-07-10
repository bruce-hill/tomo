% API

# Builtins

# Set
## Set.add

```tomo
Set.add : func(set: |T|, item: T -> Void)
```

Adds an item to the set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The mutable reference to the set.  | -
item | `T` | The item to add to the set.  | -

**Return:** Nothing.


**Example:**
```tomo
>> nums.add(42)

```
## Set.add_all

```tomo
Set.add_all : func(set: @|T|, items: [T] -> Void)
```

Adds multiple items to the set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `@|T|` | The mutable reference to the set.  | -
items | `[T]` | The list of items to add to the set.  | -

**Return:** Nothing.


**Example:**
```tomo
>> nums.add_all([1, 2, 3])

```
## Set.clear

```tomo
Set.clear : func(set: @|T| -> Void)
```

Removes all items from the set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `@|T|` | The mutable reference to the set.  | -

**Return:** Nothing.


**Example:**
```tomo
>> nums.clear()

```
## Set.has

```tomo
Set.has : func(set: |T|, item: T -> Bool)
```

Checks if the set contains a specified item.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The set to check.  | -
item | `T` | The item to check for presence.  | -

**Return:** `yes` if the item is present, `no` otherwise.


**Example:**
```tomo
>> |10, 20|.has(20)
= yes

```
## Set.is_subset_of

```tomo
Set.is_subset_of : func(set: |T|, other: |T|, strict: Bool = no -> Bool)
```

Checks if the set is a subset of another set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The set to check.  | -
other | `|T|` | The set to compare against.  | -
strict | `Bool` | If `yes`, checks if the set is a strict subset (does not equal the other set).  | `no`

**Return:** `yes` if the set is a subset of the other set (strictly or not), `no` otherwise.


**Example:**
```tomo
>> |1, 2|.is_subset_of(|1, 2, 3|)
= yes

```
## Set.is_superset_of

```tomo
Set.is_superset_of : func(set: |T|, other: |T|, strict: Bool = no -> Bool)
```

Checks if the set is a superset of another set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The set to check.  | -
other | `|T|` | The set to compare against.  | -
strict | `Bool` | If `yes`, checks if the set is a strict superset (does not equal the other set).  | `no`

**Return:** `yes` if the set is a superset of the other set (strictly or not), `no` otherwise.


**Example:**
```tomo
>> |1, 2, 3|.is_superset_of(|1, 2|)
= yes

```
## Set.overlap

```tomo
Set.overlap : func(set: |T|, other: |T| -> |T|)
```

Creates a new set with items that are in both the original set and another set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The original set.  | -
other | `|T|` | The set to intersect with.  | -

**Return:** A new set containing only items present in both sets.


**Example:**
```tomo
>> |1, 2|.overlap(|2, 3|)
= |2|

```
## Set.remove

```tomo
Set.remove : func(set: @|T|, item: T -> Void)
```

Removes an item from the set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `@|T|` | The mutable reference to the set.  | -
item | `T` | The item to remove from the set.  | -

**Return:** Nothing.


**Example:**
```tomo
>> nums.remove(42)

```
## Set.remove_all

```tomo
Set.remove_all : func(set: @|T|, items: [T] -> Void)
```

Removes multiple items from the set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `@|T|` | The mutable reference to the set.  | -
items | `[T]` | The list of items to remove from the set.  | -

**Return:** Nothing.


**Example:**
```tomo
>> nums.remove_all([1, 2, 3])

```
## Set.with

```tomo
Set.with : func(set: |T|, other: |T| -> |T|)
```

Creates a new set that is the union of the original set and another set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The original set.  | -
other | `|T|` | The set to union with.  | -

**Return:** A new set containing all items from both sets.


**Example:**
```tomo
>> |1, 2|.with(|2, 3|)
= |1, 2, 3|

```
## Set.without

```tomo
Set.without : func(set: |T|, other: |T| -> |T|)
```

Creates a new set with items from the original set but without items from another set.

Argument | Type | Description | Default
---------|------|-------------|---------
set | `|T|` | The original set.  | -
other | `|T|` | The set of items to remove from the original set.  | -

**Return:** A new set containing items from the original set excluding those in the other set.


**Example:**
```tomo
>> |1, 2|.without(|2, 3|)
= |1|

```

# Table
## Table.xor

```tomo
Table.xor : func(a: |T|, b: |T| -> |T|)
```

Return set with the elements in one, but not both of the arguments. This is also known as the symmetric difference or disjunctive union.

Argument | Type | Description | Default
---------|------|-------------|---------
a | `|T|` | The first set.  | -
b | `|T|` | The second set.  | -

**Return:** A set with the symmetric difference of the arguments.


**Example:**
```tomo
>> |1, 2, 3|.xor(|2, 3, 4|)
= |1, 4|

```
