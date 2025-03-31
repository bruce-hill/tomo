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
- [Enums](enums.md)
- [Floating point numbers](nums.md)
- [Integers](integers.md)
- [Languages](langs.md)
- [Paths](paths.md)
- [Random Number Generators](rng.md)
- [Sets](sets.md)
- [Structs](structs.md)
- [Tables](tables.md)
- [Text](text.md)
  - [Text Pattern Matching](patterns.md)

## Built-in Functions

### `ask`
Gets a line of user input text with a prompt.

```tomo
func ask(prompt:Text, bold:Bool = yes, force_tty:Bool = yes -> Void)
```

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
Exits the program with a given status and optionally prints a message.

```tomo
func ask(message:Text? = !Text, status:Int32 = 1[32] -> Void)
```

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

### `print`
Prints a message to the console (alias for [`say`](#say)).

```tomo
func print(text:Text, newline:Bool = yes -> Void)
```

- `text`: The text to print.
- `newline`: Whether or not to print a newline after the text.

**Returns:**  
Nothing.

**Example:**  
```tomo
print("Hello ", newline=no)
print("world!")
```

---

### `say`
Prints a message to the console.

```tomo
func say(text:Text, newline:Bool = yes -> Void)
```

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
Pause execution for a given number of seconds.

```tomo
func sleep(seconds: Num -> Void)
```

- `seconds`: How many seconds to sleep for.

**Returns:**  
Nothing.

**Example:**  
```tomo
sleep(1.5)
```

---

### `fail`
Prints a message to the console, aborts the program, and prints a stack trace.

```tomo
func fail(message:Text -> Abort)
```

- `message`: The error message to print.

**Returns:**  
Nothing, aborts the program.

**Example:**  
```tomo
fail("Oh no!")
```
