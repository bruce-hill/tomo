# Tables

Tables are Tomo's associative mapping structure, also known as a Dictionary or
Map. Tables are efficiently implemented as a hash table that preserves
insertion order and has fast access to keys and values as list slices. Tables
support *all* types as both keys and values.

## Syntax

Tables are written using `{}` curly braces with `=` equals signs associating key
expressions with value expressions and commas between entries:

```tomo
table := {"A"=10, "B"=20}
```

Empty tables must specify the key and value types explicitly:

```tomo
empty : {Text=Int} = {}
```

For type annotations, a table that maps keys with type `K` to values of type
`V` is written as `{K=V}`.

### Comprehensions

Similar to lists, tables can use comprehensions to dynamically construct tables:

```tomo
t := {i=10*i for i in 10}
t := {i=10*i for i in 10 if i mod 2 == 0}
t := {-1=-10, i=10*i for i in 10}
```

## Accessing Values

Table values can be accessed with square bracket indexing. The result is an
optional value:

```tomo
table := {"A"=1, "B"=2}
>> table["A"]
= 1?
>> table["missing"]
= none
```

As with all optional values, you can use the `!` postfix operator to assert
that the value is non-none (and create a runtime error if it is), or you can
use the `or` operator to provide a fallback value in the case that it's none:

```tomo
>> table["A"]!
= 1

>> table["missing"] or -1
= -1
```

### Fallback Tables

Tables can specify a fallback table that is used when looking up a value if it
is not found in the table itself:

```tomo
t := {"A"=10}
t2 := {"B"=20; fallback=t}
>> t2["A"]
= 10?
```

The fallback is available by the `.fallback` field, which returns an optional
table value:

```tomo
>> t2.fallback
= {"A"=10}?
>> t.fallback
= none
```

### Default Values

Tables can specify a default value which will be returned if a value is not
present in the table or its fallback (if any).

```tomo
counts := &{"foo"=12; default=0}
>> counts["foo"]
= 12
>> counts["baz"]
= 0
counts["baz"] += 1
>> counts["baz"]
= 1
```

When values are accessed from a table with a default value, the return type
is non-optional (because a value will always be present).

## Setting Values

You can assign a new key/value mapping or overwrite an existing one using
`.set(key, value)` or an `=` assignment statement:

```tomo
t := {"A"=1, "B"=2}
t["B"] = 222
t["C"] = 333
>> t
= {"A"=1, "B"=222, "C"=333}
```

## Length

Table length can be accessed by the `.length` field:

```tomo
>> {"A"=10, "B"=20}.length
= 2
```

## Accessing Keys and Values

The keys and values of a table can be efficiently accessed as lists using a
constant-time immutable slice of the internal data from the table:

```tomo
t := {"A"=10, "B"=20}
>> t.keys
= ["A", "B"]
>> t.values
= [10, 20]
```

## Iteration

You can iterate over the key/value pairs in a table like this:

```tomo
for key, value in table
    ...

for key in table
    ...
```

Table iteration operates over the value of the table when the loop began, so
modifying the table during iteration is safe and will not result in the loop
iterating over any of the new values.

## Table Methods

- [`func clear(t:&{K=V})`](#clear)
- [`func get(t:{K=V}, key: K -> V?)`](#get)
- [`func get_or_set(t:&{K=V}, key: K, default: V -> V)`](#get_or_set)
- [`func has(t:{K=V}, key: K -> Bool)`](#has)
- [`func remove(t:{K=V}, key: K -> Void)`](#remove)
- [`func set(t:{K=V}, key: K, value: V -> Void)`](#set)

---

### `clear`
Removes all key-value pairs from the table.

```tomo
func clear(t:&{K=V})
```

- `t`: The reference to the table.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> t.clear()
```

---

### `get`
Retrieves the value associated with a key, or returns `none` if the key is not present.
**Note:** default values for the table are ignored.

```tomo
func get(t:{K=V}, key: K -> V?)
```

- `t`: The table.
- `key`: The key whose associated value is to be retrieved.

**Returns:**  
The value associated with the key or `none` if the key is not found.

**Example:**  
```tomo
>> t := {"A"=1, "B"=2}
>> t.get("A")
= 1?

>> t.get("????")
= none

>> t.get("A")!
= 1

>> t.get("????") or 0
= 0
```

---

### `get_or_set`
If the given key is in the table, return the associated value. Otherwise,
insert the given default value into the table and return it.

**Note:** If no default value is provided explicitly, but the table has a
default value associated with it, the table's default value will be used.

**Note:** The default value is only evaluated if the key is missing.

```tomo
func get_or_set(t: &{K=V}, key: K, default: V -> V?)
```

- `t`: The table.
- `key`: The key whose associated value is to be retrieved.
- `default`: The default value to insert and return if the key is not present in the table.

**Returns:**  
Either the value associated with the key (if present) or the default value. The
table will be mutated if the key is not already present.

**Example:**  
```tomo
>> t := &{"A"=@[1, 2, 3]; default=@[]}
>> t.get_or_set("A").insert(4)
>> t.get_or_set("B").insert(99)
>> t
= &{"A"=@[1, 2, 3, 4], "B"=@[99]}

>> t.get_or_set("C", @[0, 0, 0])
= @[0, 0, 0]
>> t
= &{"A"=@[1, 2, 3, 4], "B"=@[99], "C"=@[0, 0, 0]}
```

---

### `has`
Checks if the table contains a specified key.

```tomo
func has(t:{K=V}, key: K -> Bool)
```

- `t`: The table.
- `key`: The key to check for presence.

**Returns:**  
`yes` if the key is present, `no` otherwise.

**Example:**  
```tomo
>> {"A"=1, "B"=2}.has("A")
= yes
>> {"A"=1, "B"=2}.has("xxx")
= no
```

---

### `remove`
Removes the key-value pair associated with a specified key.

```tomo
func remove(t:{K=V}, key: K -> Void)
```

- `t`: The reference to the table.
- `key`: The key of the key-value pair to remove.

**Returns:**  
Nothing.

**Example:**  
```tomo
t := {"A"=1, "B"=2}
t.remove("A")
>> t
= {"B"=2}
```

---

### `set`
Sets or updates the value associated with a specified key.

```tomo
func set(t:{K=V}, key: K, value: V -> Void)
```

- `t`: The reference to the table.
- `key`: The key to set or update.
- `value`: The value to associate with the key.

**Returns:**  
Nothing.

**Example:**  
```tomo
t := {"A"=1, "B"=2}
t.set("C", 3)
>> t
= {"A"=1, "B"=2, "C"=3}
```
