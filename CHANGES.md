# Changes

## Unreleased

- `tomo format` no longer expands inline C that fits on one line into a
  three-line block. It tested `InlineCCode.type`, which the compiler fills in
  and the parser doesn't, so the check was never true while formatting and every
  inline C expression in a multi-line context grew. `tomo format --check` could
  not see this, since it compares syntax trees and reparsing dedents a verbatim
  block, so `test/format/` now holds snapshots of the layout the formatter
  produces, checked by `make test` (regenerate with `make regen-format-tests`).

- `Text.lines()` dropped a final line exactly one grapheme long, so
  `"a\nb".lines()` returned `["a"]` and `"x".lines()` returned `[]`. Any text
  whose last line was a single character silently lost it. This also made
  `tomo format` corrupt inline C code, since the formatter splits verbatim
  chunks into lines: `sizeof(@x)` came back as `sizeof(@(x)`.

- Struct definitions take a `packed_bools` flag, which stores each `Bool` field
  in one bit and each `Bool?` field in two (`yes`, `no`, and `none` all fit), so
  adjacent boolean fields share bytes: a struct of eight `Bool` fields goes from
  eight bytes to one, and five `Bool?` fields from five to two. Fields of any
  other type keep their natural size and alignment. Previously every `Bool` field of every struct was bit-packed
  whether or not it saved anything, which is now what the flag opts into.


## 2026-08-27

- `tomo run --debug prog.tm` runs a program under gdb, in Tomo's terms.
  Breakpoints, stepping, and backtraces already worked on `.tm` files (the
  generated C carries `#line` directives back to the source); what is new is
  everything above that. Values print through Tomo's own formatter, syntax
  coloring and all, including lists, tables, and optionals. A `--debug` build
  emits the type information for each variable beside it, which is the only way
  a debugger can see inside a type-erased value. Function and argument names are
  Tomo's rather than the mangled C symbols, in gdb's own output as well as
  Tomo's, so a backtrace reads `helper (label=..., count=..., items=...)`.
  Commands added: `tlocals` (which inside a lambda lists the variables it closed
  over as well as its arguments) and `tframe`. `backtrace`/`bt`/`where` is
  replaced by one that prints the stack in Tomo's terms, arguments included
  (`helper(label="widgets", count=7, items=[1, 2, 3])`), with values cut short;
  gdb's own remains as `info stack`. A Tomo
  variable `x` is `_$x` in the generated C, so `p` (gdb's alias for `print`) is
  replaced by one that understands Tomo names. Without it, `print log` in a
  program with a variable named `log` answers with libm's `log` function.
  `print` and `watch` themselves are untouched, and anything `p` does not
  recognize is passed to `print` unchanged. An exact `Num` whose value no decimal
  expresses is shown with a rounded decimal beside it (`32768/3 ~= 10922.6666666667`,
  `pi ~= 3.1415926536`); `set tomo-num-digits` controls the digits. Printed values are cut off at gdb's
  own `print elements`, since a Tomo value is formatted whole and everything in
  scope is printed at every stop. `frame`, `up`, and `down` report
  the frame they select the way a stop is reported, with the source around the
  line and the variables in scope.

  A new `breakpoint()` builtin stops the program where it is called; it
  compiles to nothing at all without `--debug`, so leaving one in the source
  costs a release build nothing. Runtime errors stop in the debugger too, with
  the failing frame and its variables intact, as do fatal signals; `Ctrl-C`
  stops the program without killing it. A program that runs to completion
  leaves the debugger with its own exit status.

  `--debug` also works on `build`, `transpile`, and `test`. It compiles at
  `-O0` by default (an explicit `-O` still wins) and keeps its DWARF
  uncompressed, so an external debugger can read it. See `docs/debugging.md`.

- Fixed three source-mapping bugs the debugger work turned up, each of which
  made generated code claim to be somewhere it wasn't:

  - A `match` statement compiled to a `MATCH(...)` macro whose argument was the
    whole `switch` body. A preprocessor directive inside a macro argument is
    undefined, so the `#line` directives in the clause bodies were dropped and
    every line of every clause was attributed to the `match` line itself. The
    macro is now written out, and each clause maps to its own lines.
  - Compiler-synthesized C fragments dropped into the middle of an expression
    carried a `#line` of their own, which moved the line counter backwards
    mid-statement and left everything after it, to the end of the function,
    attributed to whatever line the fragment came from.
  - The generated command-line wrapper claimed to be lines of the `.tm` file,
    counting on from its first line; it is now attributed to the generated C
    file it actually is.

- Fixed a list or table comprehension being rejected in argument position:
  `f([i for i in 3])` failed with "the function takes these args: `([Int])`,
  but it's being called with: `([Int])`", while binding it to a variable first
  worked. Checking a literal against a parameter type compared each element
  against the item type, but a comprehension is not an element: it stands for
  the items it produces, and its own type is the whole list type. It is now
  looked through, in its loop's scope, so what is checked is what it yields.
  The table case was skipped outright rather than checked, so a table
  comprehension's entries are now checked too.

- **Breaking:** `sleep()`, `Text.distance()`, and `List.sample()` now use `Num`
  rather than `Float64`, so the default numeric type is what the standard
  library reaches for. `sleep()` takes a `Num` (`sleep(1/100)` is exact now),
  `Text.distance()` returns one, and `List.sample()` takes `[Num]` weights and
  a `func(->Num)` random source. Code passing an explicitly-`Float64` value to
  these needs a conversion; decimal literals like `sleep(1.5)` are unaffected,
  since they were already `Num`. `Text.distance()` still computes in floating
  point internally and converts at the end, and its costs are sums of 1, 0.5
  and 0.25, all exact in binary floating point, so the result is lossless.

- Programs can now define git-style CLI subcommands by declaring functions
  named `main.foo(...)` (nested arbitrarily deep, e.g. `main.submodule.init`).
  Each subcommand gets the same automatic argument parsing, `--help` text, and
  manpage documentation that `main()` gets, with doc comments above each
  function used as its one-line summary in the generated command listings.
  Running the program with no arguments (or an unknown command) prints a usage
  listing of the available commands, unless the program defines `main()`.
  A command can both do something itself and have sub-subcommands (like
  `git stash`/`git stash pop`): dispatch descends
  into the longest matching command path. A program with subcommands can also
  define a plain `main()`, which runs when the first argument doesn't name a
  subcommand (`./myprog` runs `main()`, `./myprog foo` runs `main.foo()`).
  Subcommand functions are also ordinary functions: they can be called directly
  (`main.add(...)`), used as function values, and accessed from other files
  (`mod.main.add(...)`).
- Fixed three bugs in command-line parsing of flags whose value is embedded in
  the argument itself (`--flag=value`):
  - `--flag=value` silently discarded every argument after it, so
    `myprog --name=x --loud` parsed `name` and then ignored `--loud` entirely.
    Only the flag's own token is consumed now.
  - `--flag=a,b,c` and `-f=a,b,c` on a list- or table-valued flag crashed
    (segfault or hang): the comma-separated values were appended to the list
    being iterated instead of the output list, with a mismatched item size. The
    space-separated forms were unaffected.
  - As a result of the above, list and table flags given in the comma form
    parsed as empty.
- New `Text.nearest(candidates, max_distance=0.6)` method: the closest of
  `candidates` to a text, or `none` when nothing is close enough. `max_distance`
  is an edit distance *per grapheme* of the shorter text, so short words have to
  match more exactly than long ones, since a flat distance can't tell
  `trasnpil` (a typo for `transpile`) from `cat` (not a typo for `fmt`),
  since both are 2 edits away. This is the "Did you mean ...?" logic that compiler errors already
  used, now available to Tomo code and shared with CLI parsing.
- `tomo fmt` is now `tomo format`. `fmt` remains as an alias, so
  nothing that already invokes it has to change. CLI commands can declare one
  alias each; an alias is accepted wherever the command name is, is shown
  under the command's own `--help`, and can be suggested for a typo, but it
  doesn't take up a line of its own in the list of commands.
- An unrecognized subcommand now suggests the closest match, in both generated
  programs and `tomo` itself, alongside the full list of available commands.
- Fixed three bugs in `Text.distance()`. They only ever ran in compiler error
  paths just before exit, so the damage went unnoticed until CLI parsing
  started using it:
  - A heap buffer overflow: it allocated its distance matrix at
    `sizeof(uint32_t)` per entry but stored `double`s in it, writing twice past
    the end of the allocation and corrupting neighbouring heap objects.
  - The matrix was indexed with a row stride of `b.length` when each row holds
    `b.length + 1` columns, so every row overlapped the next one's first cell
    and the results were wrong: `"flaw".distance("lawn")` returned 3 instead
    of 2, and `"abcd".distance("bcda")` returned 3 instead of 2.
  - The transposition check compared `b[i-2]` instead of `b[j-2]`, so
    transpositions were detected at the wrong offset whenever the two texts
    were different lengths.
- **Breaking:** `Text.distance()` no longer takes a `language` argument, and
  its half-cost discount for two graphemes that differ only in case is now
  ASCII-only. `Bool.parse()` likewise now does its own ASCII comparison, so it
  accepts `true`, `TRUE`, and `True` but no longer scrambled casings like
  `tRuE`. Both previously went through libunistring's case folding, which
  linked its case-mapping tables into every compiled program, since
  `Text.nearest()` and `Bool`'s metamethods keep them reachable. A non-ASCII
  case mismatch now scores as a full substitution instead of half, which nudges
  a suggestion's ranking rather than breaking it.
- Command-line help, usage, and error messages now respect `NO_COLOR` and
  `COLOR=0`, and drop their color when output isn't a terminal. The escapes
  were previously unconditional, so piping `--help` into a file or pager (or
  setting `NO_COLOR`) still got them. This covers `tomo` and compiled programs
  alike, and `print_err()` no longer hardcodes its red highlighting either.
  The CLI's escapes now all come from one palette (`tomo_cli_style()`), which
  returns empty strings when color is off, the same approach `report_style()`
  already used for `tomo test`/`tomo format` output.
- The `tomo` compiler now runs the same `tomo_init()` startup that every
  compiled Tomo program runs, instead of an inlined partial copy of it. Along
  with removing the duplicated color and hash-key setup, this means the
  compiler finally gets four things the copy was missing: GMP allocating
  through the GC (its bigint allocations were never freed), `setlocale()`,
  fatal-signal handlers that turn a compiler crash into a stack trace, and
  `atexit` cleanup.
- Fixed an argument-parsing error message that colorized its output
  unconditionally (the "Unsupported type" path for an argument type the parser
  doesn't handle).
- The automatic `--help`/`--version` short aliases (`-h`, `-v`) are no longer
  claimed when the command or a global flag already declares that letter, so a
  program whose `main()` takes `verbose|v` still gets `-v` for its own flag.
  They're popped before the command's own arguments are parsed, so without this
  they would shadow it.
- `tomo --version` now works; previously only `tomo version` did.
- A mistyped `tomo` command (`tomo bulid foo.tm`) now reports the bad command
  name and lists the available commands. Previously the whole command line was
  shimmed to `tomo run`, so it failed with a confusing "path not found".
  More generally, when a command has subcommands and the first word resembles
  one of them, that's now mentioned in any argument-parsing error for that
  command. The resemblance alone never blocks anything, since it's only
  reported once parsing has failed on its own, so an argument that happens
  to look like a command name still works (`tomo tests` runs a directory called
  `tests`, one edit away from the `test` command).
- Command-line dispatch and help rendering are now one implementation, shared
  by the `tomo` compiler and the programs it compiles. Previously the two had
  only argument parsing in common: `tomo` dispatched at runtime from a
  `cli_spec_t`, while generated programs got a separate tree of dispatch
  functions and help text baked in by the compiler, and the two had already
  drifted apart in what their help looked like. Commands now nest
  (`cli_command_t` has children), a program's `cli_spec_t` has a root command
  whose own arguments run when the first word isn't a subcommand, and the
  compiler emits a static `cli_spec_t` instead of code. Two behaviours that
  genuinely differ between the two are now explicit flags on `cli_spec_t`:
  `strict_positionals` (whether only args marked positional can be filled
  positionally) and `passthrough_after_double_dash` (whether `--` separates
  relayed arguments, as in `tomo run prog.tm -- args...`, or just marks the
  end of flags). As a result, generated programs' `--help` now uses the same
  layout as `tomo`'s, with `Arguments:`/`Flags:`/`Commands:` sections.
- Negative numbers can now be passed as command-line arguments. A dashed token
  is read as a value rather than a flag when the argument it fills is numeric,
  so `--count -1`, `--count=-1`, `-c-1`, and a bare positional `-1` all work,
  as do lists and tables of numbers (`--nums -1 2 -3`). Every one of these was
  previously `Not a valid flag: -1`, and a negative number could only be given
  after a bare `--`. The rule follows the argument's type rather than the token,
  so a `Text` argument given `-1` is still a usage error (`\-1` or `--` passes
  it), and a numeric argument given `-x` still is too. What counts as a number is
  whatever the argument's own type parses, so `-0x10`, `-0o644`, `-1e5`, and
  `-inf` (for `Float64`/`Float32`) all work; a number out of range for its
  argument reports that instead of being rejected as a flag. A negative number is never read as a cluster of short flags,
  so a flag whose letter appears inside one (`-e` and `-1e5`, `-x` and `-0x1f`)
  no longer claims it.
- Optional lists, tables, sets, and booleans work as command-line arguments.
  Passing a value for one (`--nums 1 2`, `--defs a:1`, `--tags a b`,
  `--force yes`) failed with `Unsupported type`, even though the generated help
  advertised the argument; only `--flag none` worked. An optional boolean is now
  the toggle its help text already claimed it was: `--force`, `--no-force`, and
  `--force=yes|no`, with `--force=none` for the third value. Where previously
  the bare flag reported `No value provided` and `--force none` silently set it
  to `no` rather than `none`. An optional list or table now also splits
  `--nums=1,2` on commas and stops at a flag (`--nums --other` gives an empty
  list) exactly like a non-optional one; the two disagreed on both.
- Added `make test-cli`, a C-level test suite for the argument parser that the
  compiler and the programs it compiles share (`src/stdlib/cli.c`). It covers
  the different ways to pass flags, the supported argument types, positional
  filling, `--`, the generated usage and help text, command dispatch, and the
  parse errors; it runs as part of `make test`.

## 2026-08-21

- `tomo run`, `tomo eval`, and `tomo test` now compile at `-O0` instead of
  `-O1`. Clang's `-O1` runs nearly the whole optimization pipeline, so it cost
  as much to compile as `-O3` while these commands throw the executable away
  after a single run. Generated code leans on precompiled library routines, so
  the runtime penalty is small (measured at ~2% on an integer-heavy loop). As
  before, `-O0` builds are instrumented with UBSan in trap mode, so undefined
  behaviour, including in hand-written `C_code`, now aborts rather than
  misbehaving silently on these paths. Pass `-O1` explicitly to get the old
  behaviour.
- Compiling a Tomo file now preloads a precompiled `tomo.h`, cached per
  toolchain configuration under the XDG cache directory and rebuilt whenever
  the installed headers change. This cuts the fixed per-file cost of parsing
  the ~40 standard library headers out of every compile. Set `TOMO_NO_PCH=1` to
  turn it off. Together with the `-O0` change, `tomo test` over the compiler's
  own test suite went from 2.0s to 1.1s.
- Fixed a `GC_MALLOC` macro redefinition between `tomo/util.h` and the bundled
  `gc.h` that made compiling against the installed headers fail under
  `-Werror`. It was usually masked by the Zig toolchain's compilation cache
  returning an object built before the clash appeared.
- **Breaking:** serialization and deserialization now have dedicated syntax,
  and the implicit conversions to and from `[Byte]` are gone. Use
  `serialize(value)` instead of `bytes : [Byte] = value`, and
  `deserialize:T(bytes)` instead of `value : T = bytes`. Because the result
  type depends on the type written after the colon, `deserialize:T(...)` is a
  construct rather than a function call: the type has to be typed out literally
  there.
  - `deserialize:T(...)` returns a `T?` instead of aborting the program on bad
    input. It gives back `none` when the bytes aren't a well-formed encoding of
    `T`: truncated data, a nonsensical length or enum tag, or leftover trailing
    bytes. Since the encoding doesn't record which type it came from, a
    well-formed encoding of some *other* type can still deserialize
    successfully into a nonsensical value, but corrupt or hostile input can
    no longer read out of bounds or trigger an enormous allocation.
  - Tomo has no nested optionals, so `deserialize:T?(...)` is still just a
    `T?`, where `none` means either "this didn't decode" or "this decoded a
    `none`". Serialize a list if you need to tell those apart.
  - Serializing a type that has no byte representation (anything containing a
    function, closure, or type object) is now a compile-time error instead of a
    runtime failure.
  - `serialize` and `deserialize` are soft keywords: `serialize` is only this
    construct when it's directly followed by `(`, and `deserialize` only when
    it's directly followed by `:`, so both remain usable as ordinary
    identifiers elsewhere. A function or closure named `serialize` would be
    unreachable through `serialize(...)`, so that's a compile error rather than
    a silent shadowing; `deserialize` is unaffected, since `deserialize:T(...)`
    can't be confused with a call.
- Fixed a buffer overflow in `CString` deserialization, which wrote its
  terminating NUL one byte past the end of its allocation.
- Fixed `Int16` serialization and deserialization, which always failed because
  they compared `fwrite()`/`fread()`'s return value (an item count) against a
  byte count.
- Deserializing a `Bool` now rejects any byte other than 0 or 1. A `2` would
  previously decode into the in-memory representation of a `none` `Bool?`.
- Deserializing a `Text` now rejects byte sequences that aren't valid UTF-8
  (or that contain a NUL). Such a `Text` was previously constructed in an
  invalid state, and using it later aborted the program.
- Fixed a compiler segfault when reading a field through an optional value
  (e.g. `p.next.name` where `next` is a `@Foo?`). Generating the "did you
  mean?" suggestion dereferenced a null namespace, and the compiler died with
  no diagnostic at all. Suggestions now look through the optional, and the
  error explains that the value needs a `!` rather than claiming the field
  doesn't exist.

## 2026-08-20

- Removed the special restrictions on struct/enum fields (and function
  arguments) whose names start with an underscore. Previously, such fields
  couldn't be read from outside the type definition (`x._foo`) and couldn't be
  passed positionally to a constructor or function call, only by keyword.
  None of that applies anymore; a leading underscore is just a regular,
  unrestricted identifier character.

## 2026-08-19

- **Breaking:** structs and enum variants are now built with curly braces
  instead of parentheses, so building a record is syntactically distinct from
  calling a function. Struct definitions take a braced field list (`struct
  Foo{x,y:Int}`), enum variants take braced fields while the variant list keeps
  its parens (`enum Baz(A{foo:Foo}, B)`), and construction and pattern matching
  both use braces (`Foo{1,2}`, `Baz.A{f}`, `when b is A{foo}`). Parentheses on a
  type name now mean only "call a constructor function", so conversions like
  `Int("5")` and `Text(5)` are unchanged, while `Foo(1,2)` on a struct or
  variant is a compile error pointing at the brace form. A variant with no
  fields is still written bare (`Baz.B`, `is B`). Struct and enum values also
  convert to text using braces now (`Foo{x=1, y=2}`), so printed output
  round-trips as source.

## 2026-08-18

- `list.pairs()` and `table.entries()` used directly in for-position (a loop,
  comprehension, reducer, or lockstep clause) now compile to inline index
  loops instead of allocating an iterator closure and calling it indirectly
  once per iteration. This is a semantics-preserving optimization: behavior,
  including snapshot semantics, is identical to iterating the closure, but a
  pairs-heavy loop measured ~6x faster. Storing the iterator in a variable
  first (`p := xs.pairs(); for a, b in p`) still uses the closure, as it must.
- New `table.entries()` and `list.pairs()` iterator methods, and direct table
  iteration is removed. `t.entries()` yields each key/value pair
  (`for k, v in t.entries()`) and `xs.pairs()` yields each unordered pair of
  distinct elements once (i < j, e.g. `(+: a.dist(b) for a, b in
  points.pairs()) or 0.0`). Both are multi-value out-parameter iterators, so
  they compose with `at` counters, `_` discards, lockstep iteration,
  comprehensions, and reducers, and both have snapshot semantics (mutating
  the container after making the iterator doesn't affect what it yields).
  `for k, v in t` is now a compile error pointing at `.entries()` (or
  `.keys`/`.values`), so loop variables always bind exactly what the
  iterable yields, and there's no more special case where a table yields a
  different number of values than everything else. Sets still iterate their
  elements directly (`for x in a_set`), which stays unambiguous at one value
  per iteration.
- New lockstep iteration: `for x, y in xs, ys` iterates over several iterables
  at once, advancing all of them together and ending as soon as any one runs
  out. Each iterable's yielded values bind to its slice of the loop variables
  in order, and the total arity must match exactly (a mismatch is a compile
  error naming each iterable's yield count). Any iterable kind can
  participate: lists, counts, ranges, text, tables (key + value), and
  iterator functions (including multi-value out-parameter iterators, e.g.
  `for k1, v1, k2, v2 in entries1, entries2`). `_` discards a value, `at`
  binds an `Int64` iteration counter, and `skip`/`stop`/`else`,
  comprehensions, and reducers all work as usual (e.g. the dot product
  `(+: x*y for x, y in xs, ys) or 0`). Iterables are evaluated once each,
  left to right, before the loop begins. In container literals the comma
  binds to the comprehension's iterables, so `[x for x in xs, 99]` is now an
  arity error; put plain items before the comprehension instead.
- Fixed: the code formatter dropped the closing parenthesis of reduction
  expressions (`(+: x for x in xs)` formatted as `(+: x for x in xs`).
- Fixed: a reducer inside a lambda didn't capture the variables in its
  iterable expression (`func(-> Int) return (+: x*2 for x in xs) or 0`
  failed to compile with an undeclared-identifier error in the generated C).
- New in-place list iteration: `for &x in xs` (and `for &x at i in xs`) yields a
  live `&` reference to each element of a mutable list, so element updates are
  direct in-place stores with no per-element bounds checks or copy-on-write
  guards. The list stays usable inside the body (reads and indexed writes see
  live data); resizing the list or making a copy of it while the loop runs is
  a clean runtime failure, checked every iteration and once after the loop,
  so a violation in the final iteration is still caught. The reference is a
  non-escaping `&` pointer, so it can't outlive its iteration.
- New `at` loop clause: `for x at i in xs` binds `i` as a native `Int64`
  iteration counter (1, 2, 3, ...) alongside the yielded value `x`. This
  replaces the old leading-index form (`for i, x in xs` is now a compile
  error suggesting `at`): loop variables always bind exactly what the
  iterable yields, so a loop's meaning never depends on the type of the
  iterable. Works uniformly across lists, text, integer counts, ranges
  (`for x at step in a.to(b)`), `onward()`, and iterator functions, where
  the last four previously had no index support at all, and in
  comprehensions and reducers (e.g. `[i*x for x at i in xs]`). Table
  iteration keeps its `for k, v` key/value meaning, with `at` available
  there too.
- New multi-value iterator protocol: a function whose arguments are all
  non-escaping `&` out-parameters and whose return type is `Bool` yields one
  value per argument on each call (write the values through the
  out-parameters and return `yes`, or return `no` to finish). Loops bind
  exactly as many variables as the iterator yields (`for a, b in
  pairs_fn`; a mismatch is a compile error), the loop variables themselves
  are the storage the iterator writes into, the yielded types may differ,
  and comprehensions/reducers work the same way (e.g.
  `(+: a.dist(b) for a, b in pairs_fn) or 0.0`). The existing
  one-value `func(->T?)` iterator protocol is unchanged.
- `for x in n` now works when `n` is a native int type (`Int64`, `Int32`, ...),
  compiling to a native counting loop.
- Fixed: `stop` inside a table or text iteration loop generated invalid C
  (a `goto` to a label that was never emitted) and failed to compile.
- Fixed: a lambda that only wrote through a `&` argument (`a[] = ...`) was
  wrongly rejected by the enclosing function's unused-variable check.
- Debug builds (`-O 0`) now run with UBSan enabled in trap mode: undefined
  behavior, including in user-written `C_code`, crashes with an ILLEGAL
  INSTRUCTION report instead of silently misbehaving. (Previously `-O 0`
  failed to link outright: `zig cc` enables UBSan at -O0 but the -nostdlib
  link has no UBSan runtime; trap mode needs none.) To make generated code
  UBSan-clean, lambdas now receive their closure userdata as a `void *` and
  cast internally (so closure calls no longer go through a mismatched
  function-pointer type), function-to-closure promotion goes through a
  per-function-type shim, and the tagged small-int fast paths shift through
  `uint64_t` with explicit bounds on shift amounts instead of relying on
  formally-undefined shifts. Optimized builds are unchanged (no
  instrumentation).
- Fixed: text iteration with an iteration counter (now `for c at i in
  some_text`) generated invalid C and failed to compile. The counter is
  1-based, matching text cluster indexing.
- Optional `Num` checks (`x!`, `or` fallbacks) now compile to an inline NaN
  test instead of an out-of-line libc call, which also unblocks register
  allocation around unwraps in hot loops (~30% faster on the n-body
  benchmark's inner loop).

## 2026-08-16

- Reworked install/uninstall around programs instead of packages. The `tomo
  install` command is gone; install a program with `tomo build --install`,
  which compiles it and copies the executable (and its generated manpage) into
  the prefix's `bin/` and `man/man1/`. Installing package directories into
  `lib/` is no longer supported (`tomo package` still builds package archives).
  `tomo build --install` takes `--prefix <dir>` to install somewhere other than
  this Tomo installation's prefix, asks before overwriting existing files
  (skip with `--yes`/`-y`), and warns if the target `bin/` isn't on your
  `$PATH`.
- `tomo uninstall-self` is gone; `tomo uninstall` with no arguments now does
  the whole-installation removal (with `-y`/`--yes`). With arguments, `tomo
  uninstall <name-or-path>` removes an installed program: it deletes the binary
  only if it is a Tomo program (recognized by its embedded build info) and
  deletes the matching `man/man1/<name>.1` only if it is a Tomo-generated
  manpage (now marked with a `.\" Generated by Tomo` comment). A missing binary
  or manpage is a warning, not an error. `make uninstall` delegates to `tomo
  uninstall --yes`.
- Tomo no longer shells out to `sudo`/`doas` to write into a prefix it doesn't
  own. Commands that write to the prefix now fail with a message asking you to
  re-run with the permissions you need (for example under `sudo`).

## 2026-08-13

- Reworked logging verbosity. The global `--verbose`/`-v` and `--quiet`/`-q`
  flags are gone; each command now has its own `--verbose`/`-v` flag (and
  commands that print progress by default (`build`, `package`, `install`)
  also have `--quiet`/`-q`). Logs are grouped into categories (`[build]`,
  `[skip]`, `[cmd]`) tracked by a bitmask; `--verbose` enables all of them.
  Run-style commands (`tomo file.tm`, `run`, `eval`) are silent by default, so
  `tomo run -v file.tm` is now how you see the compiler's progress (previously
  `tomo -v file.tm`).

- Added a `tomo eval '<expr>'` command that evaluates a Tomo expression and
  prints its result, with syntax coloring when standard output is a terminal.
  The argument may be several statements separated by newlines or `;`, so
  compound inputs like `tomo eval 'use random; random.int(1, 100)'` work; the
  value of the final statement is what gets printed.

## 2026-08-10

- The install layout is now fully versioned: everything under the prefix lives
  inside a `tomo@<version>` directory per subdirectory, so multiple Tomo
  versions can coexist cleanly. Tomo's stdlib headers moved from
  `include/tomo@VER/*.h` to `include/tomo@VER/tomo/*.h` (with the umbrella
  `tomo.h` at `include/tomo@VER/tomo.h`), the vendored headers (`gc.h`,
  `gmp.h`, libunistring's) moved from the shared `include/` into
  `include/tomo@VER/`, and `lib/libtomo@VER.a` is now `lib/tomo@VER/libtomo.a`.
  Compiled programs get `-I PREFIX/include/tomo@VER` and generated code now
  says `#include <tomo.h>` instead of `#include <tomo@VER/tomo.h>`. Tomo's
  man pages now live in `man/tomo@VER/man{1,3}/` with symlinks at the
  man-visible paths (like `bin/tomo` → `bin/tomo@VER`). `make uninstall` now
  also removes the bundled toolchain in `libexec/tomo@VER` and the man pages
  (both previously leaked), without touching other versions' pages or those
  of `tomo install`ed programs.
- The CLI has been restructured from mode flags into git-style subcommands:
  `tomo run`, `tomo build` (replacing `--compile-exe`/`-e`; takes a single
  file, with `-o` to name the output; `--compile-obj`/`-c` is gone), `tomo
  transpile`, `tomo parse`, `tomo fmt` (with `-i` replacing
  `--format-inplace`), `tomo package` (with `-o` to name the archive), `tomo
  install`/`tomo uninstall`, `tomo vendor` (with `-e` replacing
  `--vendor-editable`), and `tomo info` (with `-x` replacing
  `--extract-source`). Bare `tomo file.tm [-- args]` still runs a file, and
  bare `tomo` still opens the scratch runner. Global flags like `--verbose`
  and `-O` are valid anywhere on the command line; command-specific flags only
  after their command. Usage and help text (both per-command and top-level)
  are autogenerated from the argument specs. The old mode flags (and the
  unimplemented `--changelog`) have been removed, `--version`/`-V` is now
  `tomo version`, `--prefix` is gone, and program arguments are now only
  passed after `--` (the `--args` flag is gone).
- The bundled Zig toolchain's global cache (its libc/compiler-rt builds,
  which can reach several GB) now lives inside Tomo's own cache directory
  (`~/.cache/tomo/zig`) instead of zig's default `~/.cache/zig`, so it never
  mingles with a user-run zig's cache and `tomo uninstall-self` can clear it.
  An explicit `$ZIG_GLOBAL_CACHE_DIR` in the environment is still respected,
  and the variable is stripped from the environment of programs `tomo run`
  executes (unless it was explicitly set), so a user-run zig inside a Tomo
  program keeps using its own cache.
- The bundled Zig toolchain is now shared between coresident Tomo versions:
  the real copy lives in `libexec/zig@<zig version>` and each Tomo version's
  `libexec/tomo@VER/zig` is a symlink into it, so upgrading Tomo without a
  zig pin change adds ~10MB instead of ~400MB. `tomo uninstall-self` removes
  toolchain stores that no remaining installation references.
- New `tomo uninstall-self` command: uninstalling is now the compiler's job
  instead of a `make uninstall` file list (the Makefile target now just
  delegates to it). It removes this version's files from the prefix along
  with its per-user state and cross-compilation target packs; if other Tomo
  versions are coresident in the prefix, the `tomo` and man page symlinks
  are repointed to the newest remaining one (a smooth downgrade), and they
  are only removed when no other version remains. If no `tomo` is left
  anywhere on `$PATH` afterwards, the download cache (`~/.cache/tomo`) is
  cleared too. The scratch/stdin runner state now also respects
  `$XDG_STATE_HOME`.
- New `tomo vendor -u <package>` (`--unvendor`) flag: the inverse of
  vendoring. It restores the package's `./packages.ini` entry to its first
  non-vendored fallback source (or the compiler's default pin), re-installs
  the package into the project's store (re-pinning the digest if editable
  vendoring dropped it), and deletes the vendored copy.
- `tomo transpile` now prints the generated C to stdout, formatted with
  `clang-format` and syntax-highlighted with `bat` when those are installed
  (`--raw` disables both). The `--show-codegen`/`-C` flag has been removed.

- New `--target <platform>` flag: cross-compile executables, objects, and
  packages for any supported platform (e.g. `tomo --target aarch64-macos -e
  foo.tm`). The target platform's libraries are installed on demand (with
  confirmation, or unprompted with `--install-target`) into the XDG data
  directory by downloading that platform's distribution archive.
  Cross-compiled artifacts live in `.build/<platform>/` and executables are
  suffixed with the platform name, so they never collide with native builds.

- The Tomo compiler is now built as a fully static executable using `zig cc`
  with musl libc. The vendored dependencies (Boehm GC, GMP, libunistring) are
  also built statically with the same toolchain. Building now requires `zig`
  instead of the system C compiler and the system GC/GMP/unistring dev packages.
- A pinned Zig toolchain is now bundled into the Tomo installation (under
  `libexec/tomo@VERSION/zig/`), and the installed `tomo` uses it to compile
  programs. Tomo installations are therefore self-contained: no system C
  compiler is needed to build or run Tomo programs. Programs compiled by tomo are
  themselves fully static musl binaries.
- `make` builds for the current platform; `make dist` builds a distribution
  archive (`.tar.xz`, extractable directly into an install prefix) for each
  platform in the matrix: Linux (x86_64, aarch64, riscv64, powerpc64le, s390x),
  macOS (x86_64, aarch64), and the BSDs (FreeBSD/NetBSD/OpenBSD, x86_64 +
  aarch64). 32-bit targets are excluded (Tomo requires 64-bit), as is
  loongarch64 (unsupported by the vendored Boehm GC). Linux targets
  are fully static musl; macOS and the BSDs link libc dynamically but bundle the
  vendored libraries statically. Cross-compiling needs only the host Zig, with
  no target SDK/sysroot. Zig archives are downloaded from ziglang.org and
  verified against pinned SHA-256 checksums (`vendor/zig-checksums.mk`);
  `make -C vendor download-all-zig` mirrors every platform's Zig archive.
- Runtime stack traces are symbolized in-process using a vendored libbacktrace
  and collected with the compiler's unwinder on every platform, so they work in
  fully static binaries (including inlined frames) and need no external tools.
- Removed the OpenSSL (`-lcrypto`) dependency: package digests now use a small
  built-in SHA-256 implementation.

## 2026-07-01

- Smoothed over some rough edges between CString and Text, making it easier to convert to and fro.
  CStrings are now a lot more like Text in string interpolations, debug visualization, etc.
- `Text(path)` now works as well.

## 2026-06-30

- Tweaked parsing for path literals to avoid problems with delimiters.
- Optional booleans can no longer be automatically promoted to booleans (to avoid confusion).

## 2026-06-29

- Support piping programs into `tomo` like this: `echo 'func main() say("HI")' | tomo`
- Brought back top-level executable code (outside of `main()` function).
- Improved parser error messages for common mistakes like extra colons.

## 2026-06-28

- Added support for iterating over grapheme clusters in text
- Deprecated `--run`/`-r` flag

## 2026-05-08

- Various linker flag fixes.
- Fixed some C string bugs.
- Improved code autoformatter.
- Removed usage of `strings` and `awk` in favor of inline code.
- Improved build info.

## 2026-04-27

- Add build metadata and version information, which can be retrieved with `tomo -b`.
- Change interface for `List.binary_search()` to be more flexible.

## 2026-03-16

- Added `Path.copy_to(src, dest)`

## 2026-03-14

- Added `Path.each_child()` and `Path.walk()` for iterating over files without
  the need to allocate a list of every iterated file.
- Added `Text.matches_glob(glob)` and `Path.matches_glob(glob)`
- Overhaul to package system (previously: modules)

## 2026-02-08

- Path syntax no longer requires parentheses, any value starting with `.`, `/`,
  or `~` is treated as a path.
- Changed `Path` implementation to use C-style strings instead of an `enum`
  with array components.
- Added `Text.distance(a,b)` for calculating text distances.
- `List.random()` now returns an optional value, which is `none` when the list
  is empty, instead of failing.
- Improved error messages for misspelled variables and field/method names.

## 2025-12-31

- Added support for `123.foo()` parsing the same as `(123).foo()`
- Changed `is_between()` to be bidirectional so `5.is_between(10, 1) == yes`

## 2025-12-23.2

- Fixes for OpenBSD and Mac.

## 2025-12-23

- Improved C preprocessing performance by eliminating expensive macro calls.

## 2025-12-22

- Use static linking instead of dynamic linking for the Tomo standard library
  as well as for user libraries. This produces binaries that do not depend on
  having Tomo and the library installed at runtime.
- Added `Path.writer()` and `Path.byte_writer()` for multiple successive writes

## 2025-12-21.6

- Add smarter default behavior if run without any args (REPL-like script runner)

## 2025-12-21.5

- Various fixes for versioning and builds.

## 2025-12-21.4

- Version bump and deprecated `--changelog` flag

## 2025-12-21.3

- Version bump

## 2025-12-21.2

- Update build process

## 2025-12-21

- You can now discard empty struct values.
- For an enum `Foo(A,B,C)`, the syntax `f!` now desugars to `f.A!` using the
  first tag defined in the enum.
- Error messages are more helpful for `foo.Whatever!` enum field accessing.
- Simplified logic for enums so there is less difference between enums that
  have tags with member fields and those without.
- Rename `Empty()` to `Present()` for set-like tables.
- Paths are now an `enum Path(AbsolutePath(components:[Text]), RelativePath(components:[Text]), HomePath(components:[Text]))`
- Added `enum Result(Success, Failure(message:Text))` type for indicating
  success or failure.
- Some path methods now use `Result` return types instead of failing:
  - `Path.append()`
  - `Path.append_bytes()`
  - `Path.create_directory()`
  - `Path.remove()`
  - `Path.set_owner()`
  - `Path.write()`
  - `Path.write_bytes()`
- `Path.parent()` returns `none` if path is `(/)` (file root)
- Added check for unused variables.

## 2025-11-30

### API changes

- Added `base` parameter to various `Int.parse()` methods to allow explicitly
  setting the numeric base from 1-36.

### Bugfixes

- Fixed various issues around parsing integers.

## v2025-11-29.2

### Bugfixes

- Fix for undefined behavior on enums and structs with padding.

## 2025-11-29

### Syntax changes

- Syntax for tables has changed to use colons (`{k: v}`) instead of equals
  (`{k=v}`).
- Syntax for text literals and inline C code has been simplified.
- Added metadata format instead of `_HELP`/`_USAGE`:
  ```
  HELP: "Help text"
  USAGE: "Usage text"
  MANPAGE_SYNOPSYS: "Synopsys..."
  MANPAGE_DESCRIPTION: (./description.txt)
  ```
- **Deprecated:** `extern` keyword for declaring external symbols from C.
  - Use `C_code` instead.
- **Deprecated:** postfix `?` to make values optional.
  - Explicitly optional values can be declared as `my_var : T? = value`.
- **Deprecated:** `>> ... = ...` form of doctests. They are now called "debug logs"
  and you can specify multiple values: `>> a, b, c`
- **Deprecated:** `extend` blocks
- **Deprecated:** `deserialize` operation and `.serialized()` method call
  - Instead, convert to and from `[Byte]`

### Versioning and library changes

- Tomo versioning now uses dates instead of semantic versioning.
- Tomo libraries are now installed to
  `$TOMO_PATH/lib/tomo@TOMO_VERSION/library@LIBRARY_VERSION` instead of
  `$TOMO_PATH/share/tomo_TOMO_VERSION/installed/module_LIBRARY_VERSION`
- Core libraries are no longer shipped with the compiler, they have moved to
  separate repositories.
- Library installation has been cleaned up a bit.

### Type Changes

- List indexing now gives an optional value.
- Added support for inline anonymous enums
- Accessing a field on an enum now gives an optional value instead of a boolean.
- **Deprecated**: Sets are no longer a separate type with separate methods.
  - Instead of sets, use tables with a value type of `{KeyType:Empty}`.
  - As a shorthand, you can use `{a,b,c}` instead of `{a:Empty(),
    b:Empty(), c:Empty()}` and the type annotation `{K}` as shorthand for
    `{K:Empty}`.
- Added `Empty` for a built-in empty struct type and `EMPTY` for an instance of
  the empty struct.
- Struct fields that start with underscores can be accessed again and function
  arguments that start with underscore can be passed (but only as keyword
  arguments).

### API changes

- Added `Path.lines()`.
- Added `Text.find(text, target, start=1)`.
- Added `at_cleanup()` to register cleanup functions.
- Added `recursive` argument to `Path.create_directory()` to create parent
  directories if needed.
- `setenv()` now takes an optional parameter for value, which allows for
  unsetting environment values.
- Tables now have `and`, `or`, `xor`, and `-` (minus) metamethods.
- Added `table.with(other)`, `table.without(other)`,
  `table.intersection(other)`, and `table.difference(other)`.
- Changed `list.unique()` to return a table with `Empty()` values for each
  unique list item.
- Added a `--format` flag to the `tomo` binary that autoformats your code
  (currently unstable, do not rely on it just yet).
- Standardized text methods for Unicode encodings:
  - `Text.from_utf8()`/`Text.utf8()`
  - `Text.from_utf16()`/`Text.utf16()`
  - `Text.from_utf32()`/`Text.utf32()`

### Bug fixes

- `Int.parse()` had a memory bug.
- Breaking out of a `for line in file.by_line()!` loop would leak file handle
  resources, which could lead to exhausting the number of open file handles.
  When that happens, the standard library now forces a GC collection to clean
  up resources, which can result in file handles being freed up.
- `&` references failed to propagate when accessing fields like
  `foo.baz.method()` when `foo` is a `&Foo` and `baz.method()` takes a `&Baz`.
- Optional paths no longer fail to compile when you check them for `none`.
- Text replacement no longer infinitely loops when given an empty text to replace.
- Short CLI flag aliases now no longer use the first letter of the argument.
- Stack memory was not correctly detected in some cases, leading to potential
  memory errors.

### Other changes

- Added automatic manpage generation.
- Major improvements to robustness of CLI argument parsing.

## v0.3

- Added a versioning system based on `CHANGES.md` files and `modules.ini`
  configuration for module aliases.
- When attempting to run a program with a module that is not installed, Tomo
  can prompt the user to automatically install it.
- Programs can use `--version` as a CLI flag to print a Tomo program's version
  number and exit.
- Significant improvements to type inference to allow more expressions to be
  compiled into known types in a less verbose manner. For example:
  ```tomo
  enum NumberOrText(Number(n:Num), SomeText(text:Text))
  func needs_number_or_text(n:NumberOrText)
      >> n
  func main()
      needs_number_or_text(123)
      needs_number_or_text(123.5)
      needs_number_or_text("Hello")
  ```
- Added `tomo --prefix` to print the Tomo install prefix.
- Sets now support infix operations for `and`, `or`, `xor`, and `-`.
- Added new `json` module for JSON parsing and encoding.
- Added `Path.sibling()`.
- Added `Path.has_extension()`.
- Added `Table.with_fallback()`.
- Added `Int*.get_bit()` and `Byte.get_bit()`.
- Added `Byte.parse()` to parse bytes from text.
- Added optional `remainder` parameter to `parse()` methods, which (if
  non-none) receives the remaining text after the match. If `none`, the match
  will fail unless it consumes the whole text.
- Added optional `remainder` parameter to `Text.starts_with()` and
  `Text.ends_with()` to allow you to get the rest of the text without two
  function calls.
- Improved space efficiency of Text that contains non-ASCII codepoints.
- Doctests now use equality checking instead of converting to text.
- Fixed the following bugs:
  - Negative integers weren't converting to text properly.
  - Mutation of a collection during iteration was violating value semantics.
  - `extend` statements weren't properly checking that the type name was valid.
  - Lazy recompilation wasn't happening when `use ./foo.c` was used for local
    C/assembly files or their `#include`s.
  - Memory offsets for enums with different member alignments were miscalculated.
  - Optional types with trailing padding were not correctly being detected as `none`
  - Tomo identifiers that happened to coincide with C keywords were not allowed.
  - Compatibility issues caused compilation failure on some platforms.

## v0.2

- Improved compatibility on different platforms.
- Switched to use a per-file unique ID suffix instead of renaming symbols after
  compilation with `objcopy`.
- Installation process now sets user permissions as needed, which fixes an
  issue where a user not in the sudoers file couldn't install even to a local
  directory.
- Fixed some bugs with Table and Text hashing.
- Various other bugfixes and internal optimizations.

## v0.1

First version to get a version number.
