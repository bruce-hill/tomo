% API

# Builtins

# Table
## Table.clear

```tomo
Table.clear : func(t: &{K:V} -> Void)
```

Removes all key-value pairs from the table.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `&{K:V}` | The reference to the table.  | -

**Return:** Nothing.


**Example:**
```tomo
>> t.clear()

```
## Table.difference

```tomo
Table.difference : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a table whose key/value pairs correspond to keys only present in one table, but not the other.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table.  | -

**Return:** A table containing the common key/value pairs whose keys only appear in one table.


**Example:**
```tomo
t1 := {"A": 1; "B": 2, "C": 3}
t2 := {"B": 2, "C":30, "D": 40}
assert t1.difference(t2) == {"A": 1, "D": 40}

```
## Table.get

```tomo
Table.get : func(t: {K:V}, key: K -> V?)
```

Retrieves the value associated with a key, or returns `none` if the key is not present.

Default values for the table are ignored.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The table.  | -
key | `K` | The key whose associated value is to be retrieved.  | -

**Return:** The value associated with the key or `none` if the key is not found.


**Example:**
```tomo
>> t := {"A": 1, "B": 2}
>> t.get("A")
= 1?

>> t.get("????")
= none

>> t.get("A")!
= 1

>> t.get("????") or 0
= 0

```
## Table.get_or_set

```tomo
Table.get_or_set : func(t: &{K:V}, key: K, default: V -> V?)
```

If the given key is in the table, return the associated value. Otherwise, insert the given default value into the table and return it.

If no default value is provided explicitly, but the table has a default value associated with it, the table's default value will be used.
The default value is only evaluated if the key is missing.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `&{K:V}` | The table.  | -
key | `K` | The key whose associated value is to be retrieved.  | -
default | `V` | The default value to insert and return if the key is not present in the table.  | -

**Return:** Either the value associated with the key (if present) or the default value. The table will be mutated if the key is not already present.


**Example:**
```tomo
>> t := &{"A": @[1, 2, 3]; default=@[]}
>> t.get_or_set("A").insert(4)
>> t.get_or_set("B").insert(99)
>> t
= &{"A": @[1, 2, 3, 4], "B": @[99]}

>> t.get_or_set("C", @[0, 0, 0])
= @[0, 0, 0]
>> t
= &{"A": @[1, 2, 3, 4], "B": @[99], "C": @[0, 0, 0]}

```
## Table.has

```tomo
Table.has : func(t: {K:V}, key: K -> Bool)
```

Checks if the table contains a specified key.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The table.  | -
key | `K` | The key to check for presence.  | -

**Return:** `yes` if the key is present, `no` otherwise.


**Example:**
```tomo
>> {"A": 1, "B": 2}.has("A")
= yes
>> {"A": 1, "B": 2}.has("xxx")
= no

```
## Table.intersection

```tomo
Table.intersection : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a table with only the matching key/value pairs that are common to both tables.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table.  | -

**Return:** A table containing the common key/value pairs shared between two tables.


**Example:**
```tomo
t1 := {"A": 1; "B": 2, "C": 3}
t2 := {"B": 2, "C":30, "D": 40}
assert t1.intersection(t2) == {"B": 2}

```
## Table.remove

```tomo
Table.remove : func(t: {K:V}, key: K -> Void)
```

Removes the key-value pair associated with a specified key.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The reference to the table.  | -
key | `K` | The key of the key-value pair to remove.  | -

**Return:** Nothing.


**Example:**
```tomo
t := {"A": 1, "B": 2}
t.remove("A")
>> t
= {"B": 2}

```
## Table.set

```tomo
Table.set : func(t: {K:V}, key: K, value: V -> Void)
```

Sets or updates the value associated with a specified key.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The reference to the table.  | -
key | `K` | The key to set or update.  | -
value | `V` | The value to associate with the key.  | -

**Return:** Nothing.


**Example:**
```tomo
t := {"A": 1, "B": 2}
t.set("C", 3)
>> t
= {"A": 1, "B": 2, "C": 3}

```
## Table.with

```tomo
Table.with : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a copy of a table with values added from another table

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table from which new key/value pairs will be added.  | -

**Return:** The original table, but with values from the other table added.


**Example:**
```tomo
t := {"A": 1; "B": 2}
assert t.with({"B": 20, "C": 30}) == {"A": 1, "B": 20, "C": 30}

```
## Table.with_fallback

```tomo
Table.with_fallback : func(t: {K:V}, fallback: {K:V}? -> {K:V})
```

Return a copy of a table with a different fallback table.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The table whose fallback will be replaced.  | -
fallback | `{K:V}?` | The new fallback table value.  | -

**Return:** The original table with a different fallback.


**Example:**
```tomo
t := {"A": 1; fallback={"B": 2}}
t2 = t.with_fallback({"B": 3"})
>> t2["B"]
= 3?
t3 = t.with_fallback(none)
>> t2["B"]
= none

```
## Table.without

```tomo
Table.without : func(t: {K:V}, other: {K:V} -> {K:V})
```

Return a copy of a table, but without any of the exact key/value pairs found in the other table.

Only exact key/value pairs will be discarded. Keys with a non-matching value will be kept.

Argument | Type | Description | Default
---------|------|-------------|---------
t | `{K:V}` | The base table.  | -
other | `{K:V}` | The other table whose key/value pairs will be omitted.  | -

**Return:** The original table, but without the key/value pairs from the other table.


**Example:**
```tomo
t := {"A": 1; "B": 2, "C": 3}
assert t.without({"B": 2, "C": 30, "D": 40}) == {"A": 1, "C": 3}

```
