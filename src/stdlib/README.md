# Tomo Standard Library

This directory contains all of the standard library functionality that is built
into each Tomo program. It has all the logic for core datastructures as well as
some common functionality.

Each datatype comes as a pair of headers: `datatypes/<type>.h` gives its memory
layout and nothing else, while `<type>.h` beside it declares the functions that
operate on it. The layout headers declare no functions, which is what keeps them
clear of the cycle between the datatypes and the `TypeInfo_t` that describes
them. [datatypes.h](datatypes.h) is an umbrella over all of the layouts, and
[tomo.h](tomo.h) is an umbrella over everything.

## Core Functions

- Tomo headers all in one place: [tomo.h](tomo.h)
- Tomo standard library functions: [stdlib.h](stdlib.h), [stdlib.c](stdlib.c)
- Metamethods: [metamethods.h](metamethods.h), [metamethods.c](metamethods.c)
- Siphash: [siphash.h](siphash.h), [siphash.c](siphash.c), [siphash-internals.h](siphash-internals.h)
- Utilities (header-only): [util.h](util.h)

## Core Data Types

- All layouts in one place: [datatypes.h](datatypes.h)
- Bool: [datatypes/bool.h](datatypes/bool.h), [bool.h](bool.h), [bool.c](bool.c)
- Byte: [datatypes/byte.h](datatypes/byte.h), [byte.h](byte.h), [byte.c](byte.c)
- C String: [c_string.h](c_string.h), [c_string.c](c_string.c)
- Closure: [datatypes/closure.h](datatypes/closure.h), [functiontype.h](functiontype.h), [functiontype.c](functiontype.c)
- Float: [datatypes/float.h](datatypes/float.h), [floats.h](floats.h) over [float64.h](float64.h) and [float32.h](float32.h), from the [floatX.h](floatX.h)/[floatX.c.h](floatX.c.h) templates
- Int: [datatypes/int.h](datatypes/int.h), [integers.h](integers.h) over [bigint.h](bigint.h) and [int64.h](int64.h)/[int32.h](int32.h)/[int16.h](int16.h)/[int8.h](int8.h), from the [intX.h](intX.h)/[intX.c.h](intX.c.h) templates
- List: [datatypes/list.h](datatypes/list.h), [list.h](list.h), [list.c](list.c)
- Num: [datatypes/num.h](datatypes/num.h), [num.h](num.h), [num.c](num.c), over the exact-real core in [number.h](number.h)/[number.c](number.c) (see [number-design.md](number-design.md))
- Path: [datatypes/path.h](datatypes/path.h), [path.h](path.h), [path.c](path.c)
- Present: [datatypes/present.h](datatypes/present.h)
- Result: [datatypes/result.h](datatypes/result.h), [result.h](result.h), [result.c](result.c)
- Table: [datatypes/table.h](datatypes/table.h), [table.h](table.h), [table.c](table.c)
- Text: [datatypes/text.h](datatypes/text.h), [text.h](text.h), [text.c](text.c)
- Type Infos (for representing types as values): [datatypes/typeinfo.h](datatypes/typeinfo.h), [typeinfo.h](typeinfo.h), [typeinfo.c](typeinfo.c)
- Memory: [memory.h](memory.h), [memory.c](memory.c)

## Shared Behavior

These cover a whole class of types rather than one type, which is why they stay
plural:

- Enums: [enums.h](enums.h), [enums.c](enums.c)
- Optionals: [optionals.h](optionals.h), [optionals.c](optionals.c)
- Pointers: [pointers.h](pointers.h), [pointers.c](pointers.c)
- Structs: [structs.h](structs.h), [structs.c](structs.c)

## Other

- Command line parsing: [cli.h](cli.h), [cli.c](cli.c)
- Debugger support: [debugger.h](debugger.h), [debugger.c](debugger.c)
- Failure and error reporting: [fail.h](fail.h), [fail.c](fail.c), [report.h](report.h), [report.c](report.c)
- Files (used internally only): [files.h](files.h), [files.c](files.c)
- Formatted printing: [print.h](print.h), [print.c](print.c)
- Float formatting: [fpconv.h](fpconv.h), [fpconv.c](fpconv.c), [powers.h](powers.h)
- Profiling for `--instrument` builds: [profiling.h](profiling.h), [profiling.c](profiling.c)
- Randomness: [random.h](random.h)
- Simple parsing helpers: [simpleparse.h](simpleparse.h), [simpleparse.c](simpleparse.c)
- Stack traces: [stacktrace.h](stacktrace.h), [stacktrace.c](stacktrace.c)
- Test harness for `tomo test`: [test_harness.h](test_harness.h), [test_harness.c](test_harness.c)
- Macro helpers: [mapmacro.h](mapmacro.h)
