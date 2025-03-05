# Tables

Tables are Tomo's associative mapping structure, also known as a Dictionary or
Map. Tables are efficiently implemented as a hash table that preserves
insertion order and has fast access to keys and values as array slices. Tables
support *all* types as both keys and values.

## Syntax

Tables are written using `{}` curly braces with `:` colons associating key
expressions with value expressions and commas between entries:

```tomo
table := {"A": 10, "B": 20}
```

Empty tables must specify the key and value types explicitly:

```tomo
empty := {:Text,Int}
```

For type annotations, a table that maps keys with type `K` to values of type
`V` is written as `{K,V}`.

### Comprehensions

Similar to arrays, tables can use comprehensions to dynamically construct tables:

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
= 1 : Int?
>> table["missing"]
= none : Int?
```

As with all optional values, you can use the `!` postfix operator to assert
that the value is non-none (and create a runtime error if it is), or you can
use the `or` operator to provide a fallback value in the case that it's none:

```tomo
>> table["A"]!
= 1 : Int

>> table["missing"] or -1
= -1 : Int
```

### Fallback Tables

Tables can specify a fallback table that is used when looking up a value if it
is not found in the table itself:

```tomo
t := {"A"=10}
t2 := {"B"=20; fallback=t}
>> t2["A"]
= 10 : Int?
```

The fallback is available by the `.fallback` field, which returns an optional
table value:

```tomo
>> t2.fallback
= {"A"=10} : {Text,Int}?
>> t.fallback
= none : {Text,Int}?
```

## Setting Values

You can assign a new key/value mapping or overwrite an existing one using
`:set(key, value)` or an `=` assignment statement:

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

The keys and values of a table can be efficiently accessed as arrays using a
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
for key, value in table:
    ...

for key in table:
    ...
```

Table iteration operates over the value of the table when the loop began, so
modifying the table during iteration is safe and will not result in the loop
iterating over any of the new values.

## Table Methods

- [`func bump(t:@{K,V}, key: K, amount: Int = 1 -> Void)`](#bump)
- [`func clear(t:@{K,V})`](#clear)
- [`func get(t:{K,V}, key: K -> V?)`](#get)
- [`func has(t:{K,V}, key: K -> Bool)`](#has)
- [`func remove(t:{K,V}, key: K -> Void)`](#remove)
- [`func set(t:{K,V}, key: K, value: V -> Void)`](#set)

### `bump`
Increments the value associated with a key by a specified amount. If the key is
not already in the table, its value will be assumed to be zero.

**Signature:**  
```tomo
func bump(t:@{K,V}, key: K, amount: Int = 1 -> Void)
```

- `t`: The reference to the table.
- `key`: The key whose value is to be incremented.
- `amount`: The amount to increment the value by (default: 1).

**Returns:**  
Nothing.

**Example:**  
```tomo
>> t := {"A"=1}
t:bump("A")
t:bump("B", 10)
>> t
= {"A"=2, "B"=10}
```

---

### `clear`
Removes all key-value pairs from the table.

**Signature:**  
```tomo
func clear(t:@{K,V})
```

- `t`: The reference to the table.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> t:clear()
```

---

### `get`
Retrieves the value associated with a key, or returns null if the key is not present.

**Signature:**  
```tomo
func get(t:{K,V}, key: K -> V?)
```

- `t`: The table.
- `key`: The key whose associated value is to be retrieved.

**Returns:**  
The value associated with the key or null if the key is not found.

**Example:**  
```tomo
>> t := {"A"=1, "B"=2}
>> t:get("A")
= 1 : Int?

>> t:get("????")
= none : Int?

>> t:get("A")!
= 1 : Int

>> t:get("????") or 0
= 0 : Int
```

---

### `has`
Checks if the table contains a specified key.

**Signature:**  
```tomo
func has(t:{K,V}, key: K -> Bool)
```

- `t`: The table.
- `key`: The key to check for presence.

**Returns:**  
`yes` if the key is present, `no` otherwise.

**Example:**  
```tomo
>> {"A"=1, "B"=2}:has("A")
= yes
>> {"A"=1, "B"=2}:has("xxx")
= no
```

---

### `remove`
Removes the key-value pair associated with a specified key.

**Signature:**  
```tomo
func remove(t:{K,V}, key: K -> Void)
```

- `t`: The reference to the table.
- `key`: The key of the key-value pair to remove.

**Returns:**  
Nothing.

**Example:**  
```tomo
t := {"A"=1, "B"=2}
t:remove("A")
>> t
= {"B"=2}
```

---

### `set`
Sets or updates the value associated with a specified key.

**Signature:**  
```tomo
func set(t:{K,V}, key: K, value: V -> Void)
```

- `t`: The reference to the table.
- `key`: The key to set or update.
- `value`: The value to associate with the key.

**Returns:**  
Nothing.

**Example:**  
```tomo
t := {"A"=1, "B"=2}
t:set("C", 3)
>> t
= {"A"=1, "B"=2, "C"=3}
```
