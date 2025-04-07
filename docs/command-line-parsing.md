# Command Line Parsing

Tomo supports automatic command line argument parsing for programs.
Here's a simple example:

```tomo
# greet.tm
func main(name:Text, be_excited=no):
    if be_excited
        say("Hello $name!!!")
    else:
        say("Hi $name.")
```

This program will automatically support command line argument parsing
for the arguments to `main()`:

```bash
$ tomo greet.tm 
greet: Required argument 'name' was not provided!
Signature: greet [--help] <name> [--be-excited]

$ tomo greet.tm --help
Signature: greet [--help] <name> [--be-excited]

$ tomo greet.tm "Zaphod"
Hi Zaphod. 

$ tomo greet.tm "Zaphod" --be-excited
Hello Zaphod!!!

$ tomo greet.tm --no-be-excited --name="Zaphod"
Hi Zaphod.

$ tomo greet.tm --not-a-real-argument "Bob"
greet: Unrecognized argument: --not-a-real-argument
Signature: greet [--help] <name> [--be-excited]
```

Underscores in argument names are converted to dashes when parsing command line
arguments.

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

### Enums

For enums that do not have member values (e.g. `enum Foo(Baz, Qux)`, not `enum
Foo(Baz(x:Int), Qux)`, Tomo supports automatic command line argument parsing.
Parsing is case-insensitive:

```
# foo.tm
enum Foo(One, Two, Three)
func main(foo:Foo):
    >> foo

# Signature:
$ tomo foo.tm one
>> Foo.One

$ tomo foo.tm xxx
foo: Invalid value provided for --foo; valid values are: One Two
Signature: foo [--help] <foo>
```

### Lists of Text

Currently, Tomo supports accepting arguments that take a list of text.
List-of-text arguments can be passed like this:

```tomo
# many-texts.tm
func main(args:[Text]):
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
