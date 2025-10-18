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
$ tomo -e greet.tm
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
with `tomo -e`), you can run the program with `tomo program.tm -- [program
arguments...]`. The `--` is required to separate the arguments passed to the
Tomo compiler from those being passed to your program. For example, `tomo
greet.tm -- --help` will pass the argument `--help` to your program, whereas
`tomo greet.tm --help` will pass `--help` to `tomo`.

## Positional vs Default Arguments

Any arguments with a default value must be specified with a `--flag=value` or
`--flag value`. Arguments without a default value can be specified either by
explicit `--flag` or positionally. If an argument does not have a default value
it is required and the program will report a usage error if it is missing.

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

```
# foo.tm
struct Pair(x,y:Int)

func main(pair:Pair)
    >> pair


$ tomo foo.tm -- --pair 1 2
Pair(x=1, y=2)
```

Tomo does not currently support omitting fields with default values or passing
individual struct fields by named flag.

### Enums

For enums, values can be passed using the enum's tag name and each of its
fields positionally (the same as for structs). Parsing is case-sensitive:

```
# foo.tm
enum Foo(Nothing, AnInteger(i:Int), TwoThings(i:Int, text:Text))
func main(foo:Foo)
    >> foo

$ tomo foo.tm -- Nothing
Nothing

$ tomo foo.tm -- AnInteger 123
AnInteger(123)

$ tomo foo.tm -- TwoThings 123 hello
TwoThings(i=123, text="hello")
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

$ tomo many-texts.tm one two three
>> ["one", "two", "three"] : [Text]

$ tomo many-texts.tm --args=one,two,three
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
$ tomo -e program.tm && ./program -vo outfile.txt`
```
