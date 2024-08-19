# Channels

Channels are a thread-safe message queue for communicating between threads,
although they can also be used as a general-purpose queue.

## Syntax

The syntax to create a channel is `|:T|`, where `T` is the type that will be
passed through the channel. You can also specify a maximum size for the
channel, which will cause giving to block until the recipient has gotten from
the channel if the maximum size is reached.

```tomo
channel := |:Int|
channel:give(10)
channel:give(20)
>> channel:get()
= 10
>> channel:get()
= 20

small_channel := |:Int; max_size=5|
```

## Channel Methods

### `give`

**Description:**  
Adds an item to the channel.

**Usage:**  
```markdown
give(channel:|T|, item: T) -> Void
```

**Parameters:**

- `channel`: The channel to which the item will be added.
- `item`: The item to add to the channel.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> channel:give("Hello")
```

---

### `give_all`

**Description:**  
Adds multiple items to the channel.

**Usage:**  
```markdown
give_all(channel:|T|, items: [T]) -> Void
```

**Parameters:**

- `channel`: The channel to which the items will be added.
- `items`: The array of items to add to the channel.

**Returns:**  
Nothing.

**Example:**  
```markdown
>> channel:give_all([1, 2, 3])
```

---

### `get`

**Description:**  
Removes and returns an item from the channel. If the channel is empty, it waits until an item is available.

**Usage:**  
```markdown
get(channel:|T|) -> T
```

**Parameters:**

- `channel`: The channel from which to remove an item.

**Returns:**  
The item removed from the channel.

**Example:**  
```markdown
>> channel:get()
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
