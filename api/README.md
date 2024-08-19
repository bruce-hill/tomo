# API

Tomo has a small number of built-in functions and bunch of types.

## Types

- [Arrays](arrays.md)
- [Booleans](booleans.md)
- [Channels](channels.md)
- [Enums](enums.md)
- [Floating point numbers](nums.md)
- [Integer Ranges](ranges.md)
- [Integers](integers.md)
- [Sets](sets.md)
- [Structs](structs.md)
- [Tables](tables.md)
- [Text](text.md)
- [Threads](threads.md)

## Built-in Functions

### `say`

**Description:**  
Prints a message to the console.

**Usage:**  
```markdown
say(text:Text) -> Void
```

**Parameters:**

- `text`: The text to print.

**Returns:**  
Nothing.

**Example:**  
```markdown
say("Hello world!")
```

---

### `fail`

**Description:**  
Prints a message to the console, aborts the program, and prints a stack trace.

**Usage:**  
```markdown
fail(message:Text) -> Abort
```

**Parameters:**

- `message`: The error message to print.

**Returns:**  
Nothing, aborts the program.

**Example:**  
```markdown
fail("Oh no!")
```

---
