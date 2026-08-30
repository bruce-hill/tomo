# Command Line Parsing

Tomo supports automatic command line argument parsing for programs.
Here's a simple example:

```tomo
# greet.tm
func main(name:Text, be_excited|E:Bool=no)
    if be_excited
        say("Hello $name!!!")
    else
        say("Hi $name.")
```

This program will automatically support command line argument parsing
for the arguments to `main()`:

```bash
$ tomo build greet.tm
Compiled executable: greet

$ ./greet
greet: Required argument 'name' was not provided!
Usage: greet [--help] <name> [--be-excited|-E|--no-be-exited]

$ ./greet --help
Usage: greet [--help] <name> [--be-excited|-E|--no-be-excited]

$ ./greet "Zaphod"
Hi Zaphod. 

$ ./greet "Zaphod" --be-excited
Hello Zaphod!!!

$ ./greet "Zaphod" -E
Hello Zaphod!!!

$ ./greet --no-be-excited --name="Zaphod"
Hi Zaphod.

$ ./greet --not-a-real-argument "Bob"
greet: Unrecognized argument: --not-a-real-argument
Usage: greet [--help] <name> [--be-excited|-E|--no-be-excited]
```

Underscores in argument names are converted to dashes when parsing command line
arguments.

## Running Programs Directly

If you want to run a program directly (instead of compiling to an executable
with `tomo build`), you can run the program with `tomo program.tm -- [program
arguments...]`. The `--` is required to separate the arguments passed to the
Tomo compiler from those being passed to your program. For example, `tomo
greet.tm -- --help` will pass the argument `--help` to your program, whereas
`tomo greet.tm --help` will pass `--help` to `tomo`.

## Positional vs Default Arguments

Any argument can be given either by explicit `--flag value` (or `--flag=value`)
or positionally. Positional values fill the arguments that no flag has already
filled, in the order the arguments are declared:

```tomo
func main(name:Text, count:Int=1)
    ...
```
```bash
$ ./greet Zaphod        # name="Zaphod", count=1
$ ./greet Zaphod 3      # name="Zaphod", count=3
$ ./greet --count=3 Zaphod   # same thing: --count is filled first, so the
                             # positional "Zaphod" goes to name
```

Note that this includes arguments that have a default value, like `count`
above. If an argument does *not* have a default value it is required, and the
program reports a usage error when nothing fills it.

The same command-line machinery runs both your compiled programs and the
`tomo` compiler itself, so `tomo --help` and `./yourprogram --help` are laid
out the same way.

A value that starts with `-` is rejected wherever it would be read as a value,
since it looks like a flag. To pass a text value that really does start with a
dash, either escape it with a backslash (`./greet '\-weird'`, which also
works in the `--name='\-weird'` form) or put it after a bare `--`:

```bash
$ ./greet -weird        # error: Not a valid flag: -weird
$ ./greet '\-weird'     # name="-weird"
$ ./greet -- -weird     # name="-weird"
```

Negative numbers are the exception: a dashed value is read as a number, not a
flag, when the argument it fills is a numeric one. Nothing else could be meant
by it, and it needs no escaping in any of the spellings:

```tomo
func main(count|c:Int, scale:Num=1.0)
    ...
```
```bash
$ ./scale --count -1            # count=-1
$ ./scale --count=-1            # count=-1
$ ./scale -c-1 --scale=-1.5     # count=-1, scale=-1.5
$ ./scale -1                    # count=-1, filled positionally
```

This follows the type, not the value: a `Text` argument given `-1` is still a
usage error (`\-1` or a bare `--` passes it), and a numeric argument given
`-x` is still a usage error too. What counts as a number is whatever the
argument's own type parses, so every spelling works negated -- `-0x10`,
`-0o644`, `-1e5`, and `-inf` for `Float64`/`Float32`. A value that is a number
but out of range for its argument (`-1` for a `Byte`, `-inf` for a `Num`, which
has no infinity) reports that, rather than being rejected as a flag. A list or
table of numbers takes negative values the same way, and stops at a dashed
argument that isn't one:

```bash
$ ./stats --nums -1 2 -3 --verbose   # nums=[-1, 2, -3]
```

## Supported Argument Types

Tomo automatically supports several argument types out of the box, but if there
is a type that isn't supported, you can always fall back to accepting a `Text`
argument and parsing it yourself.

### Text

Text arguments are the simplest: the input arguments are taken verbatim.

### Bool

For a boolean argument, `foo`, the argument can be passed in several ways:

- `--foo` or `--no-foo` provide the argument as `yes`/`no` respectively
- `--foo=yes`/`--foo=on`/`--foo=true`/`--foo=1` all parse as `yes` (case insensitive)
- `--foo=no`/`--foo=off`/`--foo=false`/`--foo=0` all parse as `no` (case insensitive)

An optional boolean (`foo:Bool?`) takes the same spellings, plus `--foo=none`
for the third value. A bare `--foo` is still `yes` and `--no-foo` is still `no`.

- Any other values will report a usage error

### Integers and Numbers

Integer and number values can be passed and parsed automatically. Any failures
to parse will cause a usage error. Integers support decimal (`123`),
hexadecimal (`0xFF`), and octal values (`0o644`). Nums support regular (`123`
or `1.23`) or scientific notation (`1e99`).

For fixed-size integers (`Int64`, `Int32`, `Int16`, `Int8`), arguments that
exceed the representable range for those values are considered usage errors.

### Structs

For structs, values can be passed using positional arguments for each struct
field.

```tomo
# foo.tm
struct Pair{x,y:Int}

func main(pair:Pair)
    >> pair


$ tomo foo.tm -- --pair 1 2
Pair{x=1, y=2}
```

Tomo does not currently support omitting fields with default values or passing
individual struct fields by named flag.

### Enums

For enums, values can be passed using the enum's tag name and each of its
fields positionally (the same as for structs). Parsing is case-sensitive:

```tomo
# foo.tm
enum Foo(Nothing, AnInteger{i:Int}, TwoThings{i:Int, text:Text})
func main(foo:Foo)
    >> foo

$ tomo foo.tm -- Nothing
Nothing

$ tomo foo.tm -- AnInteger 123
AnInteger{123}

$ tomo foo.tm -- TwoThings 123 hello
TwoThings{i=123, text="hello"}
```

Like structs, enums do not currently support passing fields as flags or
omitting fields with default values.

### Lists of Text

Currently, Tomo supports accepting arguments that take a list of text.
List-of-text arguments can be passed like this:

```tomo
# many-texts.tm
func main(args:[Text])
    >> args
```

```bash
$ tomo many-texts.tm
>> [] : [Text]

$ tomo many-texts.tm -- one two three
>> ["one", "two", "three"] : [Text]

$ tomo many-texts.tm -- one --not-a-flag 'a space'
>> ["one", "--not-a-flag", "a space"] : [Text]
```

## Aliases and Flag Arguments

Each argument may optionally have an alias of the form `name|alias`. This allows
you to specify a long-form argument and a single-letter flag like `verbose|v =
no`. Single letter flags (whether as an alias or as a main flag name) have
slightly different command line parsing rules:

- Single letter flags use only a single dash: `-v` vs `--verbose`
- Single letter flags can coalesce with other single letter flags: `-abc` is the
same as `-a -b -c`

When single letter flags coalesce together, the first flags in the cluster must
be boolean values, while the last one is allowed to be any type. This lets you
specify several flags at once while still providing arguments:

```tomo
func main(output|o:Path? = none, verbose|v:Bool = no)
    ...
```
```bash
$ tomo build program.tm && ./program -vo outfile.txt`
```

## Subcommands

For git-style CLIs, you can define subcommands by declaring functions named
`main.<command>` instead of a single `main()` function:

```tomo
# mygit.tm

# Initialize a new repository
func main.init(bare:Bool=no)
    ...

# Add file contents to the index
func main.add(files:[Text], force|f:Bool=no)
    ...

# Initialize submodules
func main.submodule.init(paths:[Text])
    ...
```

Each subcommand function gets the same automatic argument parsing that
`main()` gets, including flags, defaults, aliases, and `--help`:

```bash
$ tomo build mygit.tm

$ ./mygit add --force a.txt b.txt
$ ./mygit submodule init vendor/foo

$ ./mygit add --help
./mygit add: Add file contents to the index

Usage: ./mygit add --files text1 text2... [--force|-f]

Flags:
  --files text1 text2...
  -f, --force|--no-force (default:no)
```

Running the program with no command (or an unknown one) prints a listing of
the available commands, using the comment above each function as its one-line
summary:

```bash
$ ./mygit
./mygit

Usage: ./mygit <command> ...

Commands:
  init       Initialize a new repository
  add        Add file contents to the index
  submodule  <command> ...
```

Subcommands can be nested arbitrarily deep (`main.submodule.init` handles
`mygit submodule init`), and a command can both do something itself *and* have
sub-subcommands, like `git stash` and `git stash pop`:

```tomo
# Stash changes away
func main.stash(message:Text? = none)
    ...

# Remove a stash entry
func main.stash.pop(index:Int = 0)
    ...
```

Dispatch descends into the longest matching command path: `mygit stash pop`
runs `main.stash.pop()`, while `mygit stash` runs `main.stash()`.

A program with subcommands can also define a plain `main()`, which runs when
the first argument doesn't name a subcommand:

```tomo
# Greet someone by name
func main(name:Text="world")
    say("hello $name")

# Initialize a new repository
func main.init(bare:Bool=no)
    ...
```

```bash
$ ./myprog             # no subcommand: runs main()
hello world
$ ./myprog --name=Ford  # still main()
hello Ford
$ ./myprog init         # runs main.init()
```

This is the same longest-match rule used for nested commands: if the next word
names a subcommand, dispatch descends into it, otherwise the current command's
own handler runs with the remaining arguments. `main()`'s flags appear in the
root `--help` above the command listing.

If a positional argument's value happens to collide with a subcommand name,
the subcommand wins; pass the value after `--` (`./myprog -- init`) or as an
explicit flag (`./myprog --word=init`) to disambiguate.

Each flag belongs to its own subcommand: `main()`'s flags are *not* accepted
after a subcommand name, so there are no program-wide "global flags".

Underscores in subcommand names are converted to dashes on the command line,
the same as flag names: `func main.dry_run()` handles `myprogram dry-run`.

### Subcommands Are Ordinary Functions

Subcommand functions are regular functions that the compiler additionally
wires into the command-line dispatcher. You can call them directly, use them
as function values, and access them from other files:

```tomo
func main.commit(message:Text, all|a:Bool=no)
    if all
        main.add(files=modified_files(), force=no)
    ...

func main.show()
    fn := main.add
    fn(["via a function pointer"], yes)
```

Calling a subcommand function directly bypasses command-line parsing entirely:
the arguments are ordinary typed function arguments. This also makes
subcommands easy to test:

```tomo
# test.tm
mygit := use ./mygit.tm

test "adding files"
    mygit.main.add([(./foo.txt)], force=yes)
    ...
```

Subcommand functions are constants: assigning to `main.add` is a compile-time
error, and a command path prefix like `main.submodule` is not a value by
itself.

## Help and Manpages

When your program is generated, it will also come with a `--help` flag (unless
you have one defined) with automatically generated usage information. If you
add comments in front of your main function arguments, they will appear in the
`--help` output. Additionally, when your program is compiled, Tomo will also
build a Manpage for your program in `.tomo/yourprogram.1`, which will get
installed if you install your program.

```tomo
func main(
    # Whether or not to frob your gropnoggles
    frob: Bool = no
)
    pass
```

```bash
$ tomo build myprogram.tm
$ ./myprogram --help
# Usage: ./myprogram [--help] [--frob|--no-frob]
#
#  --frob|--no-frob Whether or not to frob your gropnoggles (default:no)
#
```

```bash
$ man .tomo/myprogram.1
```

```
MYPROGRAM(1)

NAME
       myprogram - a Tomo program

OPTIONS
       --frob | --no-frob
              Whether or not to frob your gropnoggles
```

## Metadata

You can specify metadata for a program, which is used for CLI messages like
`--help`, as well as manpage documentation. Metadata can be specified as either
a text literal (no interpolation) or as a file path literal.

```
USAGE: "--foo <n>"
HELP: "
    This is some custom help text.
    You can use these flags:

    --foo <n>  The foo parameter
    --help     Show this message
"
MANPAGE_DESCRIPTION: (./description.roff)
```

Supported metadata:

- `EXECUTABLE`: the name of the executable to compile. If not provided, the name
  will be the name of the Tomo source file without the ".tm" extension.

- `USAGE`: the short form usage shown in CLI parsing errors and help pages. This
  should be a single line without the name of the program, so `USAGE: "--foo"`
  would translate to the error message `Usage: myprogram --foo`. If this is not
  present, it will be generated automatically.

- `HELP`: The help message displayed when the `--help` flag is used or when there
  is an argument parsing error. This should be a description of the program with
  a multi-line documentation of commonly used flags.

- `MANPAGE`: the full manpage (overrules the options below).

- `MANPAGE_SYNOPSYS`: the synopsis section of the manpage (inserted literally).

- `MANPAGE_DESCRIPTION`: the description section of the manpage (inserted literally).
