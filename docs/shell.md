# Shell Scripting

Tomo comes with a built-in [lang](langs.md) called `Shell` for shell commands.
This lets you write and invoke shell commands in a more type-safe way.

```tomo
user_name := ask("What's your name? ")
_ := $Shell"echo Hello $user_name"
```

In the above example, there is no risk of code injection, because the
user-controlled string is automatically escaped when performing interpolation.

## Shell Methods

- [`func by_line(command: Shell -> Void)`](#by_line)
- [`func execute(command: Shell -> Int32?)`](#execute)
- [`func run(command: Shell -> Text?)`](#run)
- [`func run(command: Shell -> [Byte]?)`](#run_bytes)

### `by_line`
Run a shell command and return an iterator over its output, line-by-line.

```tomo
func by_line(command: Shell -> Void)
```

- `command`: The command to run.

**Returns:**  
An optional iterator over the lines of the command's output. If the command fails
to run, `none` will be returned.

**Example:**  
```tomo
i := 1
for line in $Shell"ping www.example.com":by_line()!:
    stop if i > 5
    i += 1
```

### `execute`
Execute a shell command without capturing its output and return its exit status.

```tomo
func execute(command: Shell -> Int32?)
```

- `command`: The command to execute.

**Returns:**  
If the command exits normally, return its exit status. Otherwise return `none`.

**Example:**  
```tomo
>> $Shell"touch file.txt":execute()
= 0?
```

---

### `run`
Run a shell command and return the output text from `stdout`.

```tomo
func run(command: Shell -> Text?)
```

- `command`: The command to run.

**Returns:**  
If the program fails to run (e.g. a non-existent command), return `none`,
otherwise return the entire standard output of the command as text. **Note:**
if there is a trailing newline, it will be stripped.

**Example:**  
```tomo
>> $Shell"seq 5":run()
= "1$\n2$\n3$\n4$\n5"
```

---

### `run_bytes`
Run a shell command and return the output in raw bytes from `stdout`.

```tomo
func run(command: Shell -> [Byte]?)
```

- `command`: The command to run.

**Returns:**  
If the program fails to run (e.g. a non-existent command), return `none`,
otherwise return the entire standard output of the command as an array of
bytes.

**Example:**  
```tomo
>> $Shell"seq 5":run_bytes()
= [0x31, 0x0A, 0x32, 0x0A, 0x33, 0x0A, 0x34, 0x0A, 0x35, 0x0A]
```
