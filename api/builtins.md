% API

# Builtins
## USE_COLOR

```tomo
USE_COLOR : Bool
```

Whether or not the console prefers ANSI color escape sequences in the output.

## ask

```tomo
ask : func(prompt: Text, bold: Bool = yes, force_tty: Bool = yes -> Text?)
```

Gets a line of user input text with a prompt.

When a program is receiving input from a pipe or writing its output to a pipe, this flag (which is enabled by default) forces the program to write the prompt to `/dev/tty` and read the input from `/dev/tty`, which circumvents the pipe. This means that `foo | ./tomo your-program | baz` will still show a visible prompt and read user input, despite the pipes. Setting this flag to `no` will mean that the prompt is written to `stdout` and input is read from `stdin`, even if those are pipes.

Argument | Type | Description | Default
---------|------|-------------|---------
prompt | `Text` | The text to print as a prompt before getting the input.  | -
bold | `Bool` | Whether or not to print make the prompt appear bold on a console.  | `yes`
force_tty | `Bool` | Whether or not to force the use of /dev/tty.  | `yes`

**Return:** A line of user input text without a trailing newline, or empty text if something went wrong (e.g. the user hit `Ctrl-D`).


**Example:**
```tomo
>> ask("What's your name? ")
= "Arthur Dent"

```
## exit

```tomo
exit : func(message: Text? = none, status: Int32 = Int32(1) -> Void)
```

Exits the program with a given status and optionally prints a message.

Argument | Type | Description | Default
---------|------|-------------|---------
message | `Text?` | If nonempty, this message will be printed (with a newline) before exiting.  | `none`
status | `Int32` | The status code that the program with exit with.  | `Int32(1)`

**Return:** This function never returns.


**Example:**
```tomo
exit(status=1, "Goodbye forever!")

```
## fail

```tomo
fail : func(message: Text -> Abort)
```

Prints a message to the console, aborts the program, and prints a stack trace.

Argument | Type | Description | Default
---------|------|-------------|---------
message | `Text` | The error message to print.  | -

**Return:** Nothing, aborts the program.


**Example:**
```tomo
fail("Oh no!")

```
## getenv

```tomo
getenv : func(name: Text -> Text?)
```

Gets an environment variable.

Argument | Type | Description | Default
---------|------|-------------|---------
name | `Text` | The name of the environment variable to get.  | -

**Return:** If set, the environment variable's value, otherwise, `none`.


**Example:**
```tomo
>> getenv("TERM")
= "xterm-256color"?

```
## print

```tomo
print : func(text: Text, newline: Bool = yes -> Void)
```

Prints a message to the console (alias for say()).

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to print.  | -
newline | `Bool` | Whether or not to print a newline after the text.  | `yes`

**Return:** Nothing.


**Example:**
```tomo
print("Hello ", newline=no)
print("world!")

```
## say

```tomo
say : func(text: Text, newline: Bool = yes -> Void)
```

Prints a message to the console.

Argument | Type | Description | Default
---------|------|-------------|---------
text | `Text` | The text to print.  | -
newline | `Bool` | Whether or not to print a newline after the text.  | `yes`

**Return:** Nothing.


**Example:**
```tomo
say("Hello ", newline=no)
say("world!")

```
## setenv

```tomo
setenv : func(name: Text, value: Text -> Void)
```

Sets an environment variable.

Argument | Type | Description | Default
---------|------|-------------|---------
name | `Text` | The name of the environment variable to set.  | -
value | `Text` | The new value of the environment variable.  | -

**Return:** Nothing.


**Example:**
```tomo
setenv("FOOBAR", "xyz")

```
## sleep

```tomo
sleep : func(seconds: Num -> Void)
```

Pause execution for a given number of seconds.

Argument | Type | Description | Default
---------|------|-------------|---------
seconds | `Num` | How many seconds to sleep for.  | -

**Return:** Nothing.


**Example:**
```tomo
sleep(1.5)

```
