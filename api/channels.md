# Channels

Channels are a thread-safe message queue for communicating between threads,
although they can also be used as a general-purpose queue.

```tomo
channel := |Int|
channel:push(10)
channel:push(20)
>> channel:pop()
= 10
>> channel:pop()
= 20
```

## Channel Methods

### `push`

**Description:**  
Adds an item to the channel.

**Usage:**  
```markdown
push(channel:|T|, item: T) -> Void
```

**Parameters:**

- `channel`: The channel to which the item will be added.
- `item`: The item to add to the channel.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> channel:push("Hello")
```

---

### `push_all`

**Description:**  
Adds multiple items to the channel.

**Usage:**  
```markdown
push_all(channel:|T|, items: [T]) -> Void
```

**Parameters:**

- `channel`: The channel to which the items will be added.
- `items`: The array of items to add to the channel.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> channel:push_all([1, 2, 3])
```

---

### `pop`

**Description:**  
Removes and returns an item from the channel. If the channel is empty, it waits until an item is available.

**Usage:**  
```markdown
pop(channel:|T|) -> T
```

**Parameters:**

- `channel`: The channel from which to remove an item.

**Returns:**  
The item removed from the channel.

**Example:**  
```markdown
>> channel:pop()
= "Hello"
```

---

### `clear`

**Description:**  
Removes all items from the channel.

**Usage:**  
```markdown
clear(channel:|T|) -> Void
```

**Parameters:**

- `channel`: The mutable reference to the channel.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> channel:clear()
```

---

### `view`

**Description:**  
Returns a list of all items currently in the channel without removing them.

**Usage:**  
```markdown
channel:view() -> [T]
```

**Parameters:**

- `channel`: The channel to view.

**Returns:**  
An array of items currently in the channel.

**Example:**  
```markdown
>> channel:view()
= [1, 2, 3]
```
