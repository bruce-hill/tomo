# Documentation

This is an overview of the documentation on Tomo.

## Topics

A few topics that are documented:

- [Compilation Pipeline](compilation.md)
- [Functions](functions.md)
- [Libraries/Modules](libraries.md)
- [Namespacing](namespacing.md)
- [Operator Overloading](operators.md)
- [Special Methods](metamethods.md)
- [C Interoperability](c-interoperability.md)

## Types

Information about Tomo's built-in types can be found here:

- [Arrays](arrays.md)
- [Booleans](booleans.md)
- [Bytes](bytes.md)
- [Channels](channels.md)
- [DateTime](datetime.md)
- [Enums](enums.md)
- [Floating point numbers](nums.md)
- [Integer Ranges](ranges.md)
- [Integers](integers.md)
- [Languages](langs.md)
- [Paths](paths.md)
- [Sets](sets.md)
- [Structs](structs.md)
- [Tables](tables.md)
- [Text](text.md)
- [Threads](threads.md)

## Built-in Functions

### `ask`

**Description:**  
Gets a line of user input text with a prompt.

**Signature:**  
```tomo
func ask(prompt:Text, bold:Bool = yes, force_tty:Bool = yes -> Void)
```

**Parameters:**

- `prompt`: The text to print as a prompt before getting the input.
- `bold`: Whether or not to print make the prompt appear bold on a console
  using the ANSI escape sequence `\x1b[1m`.
- `force_tty`: When a program is receiving input from a pipe or writing its
  output to a pipe, this flag (which is enabled by default) forces the program
  to write the prompt to `/dev/tty` and read the input from `/dev/tty`, which
  circumvents the pipe. This means that `foo | ./tomo your-program | baz` will
  still show a visible prompt and read user input, despite the pipes. Setting
  this flag to `no` will mean that the prompt is written to `stdout` and input
  is read from `stdin`, even if those are pipes.

**Returns:**  
A line of user input text without a trailing newline, or empty text if
something went wrong (e.g. the user hit `Ctrl-D`).

**Example:**  
```tomo
>> ask("What's your name? ")
= "Arthur Dent"
```

---

### `exit`

**Description:**  
Exits the program with a given status and optionally prints a message.

**Signature:**  
```tomo
func ask(message:Text? = !Text, status:Int32 = 1[32] -> Void)
```

**Parameters:**

- `message`: If nonempty, this message will be printed (with a newline) before
  exiting.
- `status`: The status code that the program with exit with (default: 1, which
  is a failure status).

**Returns:**  
This function never returns.

**Example:**  
```tomo
exit(status=1, "Goodbye forever!")
```

---

### `say`

**Description:**  
Prints a message to the console.

**Signature:**  
```tomo
func say(text:Text, newline:Bool = yes -> Void)
```

**Parameters:**

- `text`: The text to print.
- `newline`: Whether or not to print a newline after the text.

**Returns:**  
Nothing.

**Example:**  
```tomo
say("Hello ", newline=no)
say("world!")
```

---

### `sleep`

**Description:**  
Pause execution for a given number of seconds.

**Signature:**  
```tomo
func sleep(seconds: Num -> Void)
```

**Parameters:**

- `seconds`: How many seconds to sleep for.

**Returns:**  
Nothing.

**Example:**  
```tomo
sleep(1.5)
```

---

### `fail`

**Description:**  
Prints a message to the console, aborts the program, and prints a stack trace.

**Signature:**  
```tomo
func fail(message:Text -> Abort)
```

**Parameters:**

- `message`: The error message to print.

**Returns:**  
Nothing, aborts the program.

**Example:**  
```tomo
fail("Oh no!")
```

---

### `now`

**Description:**  
Gets the current time. This is an alias for `DateTime.now()`.

**Signature:**  
```tomo
func now(->DateTime)
```

**Parameters:**

None.

**Returns:**  
The current moment as a DateTime.

**Example:**  
```tomo
>> now()
= Sun Sep 29 20:12:33 2024
```
