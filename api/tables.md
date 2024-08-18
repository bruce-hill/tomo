# Tables

Tables are Tomo's associative mapping structure, also known as a Dictionary or
Map. Tables are efficiently implemented as a hash table that preserves
insertion order and has fast access to keys and values as array slices. Tables
support *all* types as both keys and values.

Tables do not support square bracket indexing (`t[key]`), but instead rely on
the methods `:get(key)` and `:set(key, value)`. This is explicit to avoid
hiding the fact that table lookups and table insertion are performing function
calls and have edge conditions like a failure to find an entry.

---

## Table Methods

### `bump`

**Description:**  
Increments the value associated with a key by a specified amount. If the key is
not already in the table, its value will be assumed to be zero.

**Usage:**  
```markdown
bump(t:{K:V}, key: K, amount: Int = 1) -> Void
```

**Parameters:**

- `t`: The mutable reference to the table.
- `key`: The key whose value is to be incremented.
- `amount`: The amount to increment the value by (default: 1).

**Returns:**  
Nothing.

**Example:**  
```markdown
>> t := {"A":1}
t:bump("A")
t:bump("B", 10)
>> t
= {"A": 2, "B": 10}
```

---

### `clear`

**Description:**  
Removes all key-value pairs from the table.

**Usage:**  
```markdown
t:clear() -> Void
```

**Parameters:**

- `t`: The mutable reference to the table.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> t:clear()
```

---

### `get`

**Description:**  
Retrieves the value associated with a key, or returns a default value if the key is not present.

**Usage:**  
```markdown
t:get(key: K, default: V) -> V
```

**Parameters:**

- `t`: The table.
- `key`: The key whose associated value is to be retrieved.
- `default`: The value to return if the key is not present. If this argument is
  not provided, a runtime error will be created if the key is not present.

**Returns:**  
The value associated with the key or the default value if the key is not found.

**Example:**  
```markdown
>> t := {"A":1, "B":2}
>> t:get("A")
= 1

>> t:get("xxx", 0)
= 0
```

---

### `get_or_null`

**Description:**  
Retrieves the value associated with a key, or returns `null` if the key is not present.
This method is only available on tables whose values are pointers.

**Usage:**  
```markdown
t:get_or_null(key: K) -> @V?
```

**Parameters:**

- `t`: The table.
- `key`: The key whose associated value is to be retrieved.

**Returns:**  
A mutable reference to the value associated with the key or `null` if the key is not found.

**Example:**  
```markdown
>> t := {"A": @[10]}
>> t:get_or_null("A")
= @[10]?
>> t:get_or_null("xxx")
= !@[Int]
```

---

### `has`

**Description:**  
Checks if the table contains a specified key.

**Usage:**  
```markdown
has(t:{K:V}, key: K) -> Bool
```

**Parameters:**

- `t`: The table.
- `key`: The key to check for presence.

**Returns:**  
`yes` if the key is present, `no` otherwise.

**Example:**  
```markdown
>> {"A":1, "B":2}:has("A")
= yes
>> {"A":1, "B":2}:has("xxx")
= no
```

---

### `remove`

**Description:**  
Removes the key-value pair associated with a specified key.

**Usage:**  
```markdown
remove(t:{K:V}, key: K) -> Void
```

**Parameters:**

- `t`: The mutable reference to the table.
- `key`: The key of the key-value pair to remove.

**Returns:**  
Nothing.

**Example:**  
```markdown
t := {"A":1, "B":2}
t:remove("A")
>> t
= {"B": 2}
```

---

### `set`

**Description:**  
Sets or updates the value associated with a specified key.

**Usage:**  
```markdown
set(t:{K:V}, key: K, value: V) -> Void
```

**Parameters:**

- `t`: The mutable reference to the table.
- `key`: The key to set or update.
- `value`: The value to associate with the key.

**Returns:**  
Nothing.

**Example:**  
```markdown
t := {"A": 1, "B": 2}
t:set("C", 3)
>> t
= {"A": 1, "B": 2, "C": 3}
```
