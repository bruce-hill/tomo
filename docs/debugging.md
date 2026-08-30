# Debugging

`tomo run --debug` runs a program under a debugger:

```bash
$ tomo run --debug fib.tm
```

The program starts immediately and runs as it normally would. It stops when
something stops it, whether a `breakpoint()` in the source, a runtime error, a
fatal signal, or `Ctrl-C`, and hands you a prompt at that point:

```
Tomo debugger -- break FILE.tm:LINE, step, next, finish, continue, bt, p VAR, tlocals, tframe, help

describe (shapes.tm:4)
   2│
   3│ func describe(p:Point, tags:[Text] -> Text)
 > 4│     breakpoint()
   5│     return "$p tagged $tags"
   6│

   p = Point{x=3, y=4}
   tags = ["alice", "bob"]
(gdb)
```

If nothing stops it, the program runs to completion and the debugger exits with
the program's own exit status, exactly as `tomo run` would have.

The debugger is [gdb](https://sourceware.org/gdb/), with a Tomo layer loaded
into it. Everything gdb can do is available and works in terms of Tomo source:
`break shapes.tm:12`, `step`, `next`, `finish`, `continue`, `list`, `watch`,
conditional breakpoints, and so on.

## Stopping on purpose: `breakpoint()`

Call `breakpoint()` anywhere in a Tomo program to stop there:

```tomo
func describe(p:Point, tags:[Text] -> Text)
    breakpoint()
    return "$p tagged $tags"
```

Only a program compiled with `--debug` stops there. Without that flag
`breakpoint()` compiles to nothing at all, not a call to an empty function but
nothing whatsoever, so leaving one in the source costs a release build
nothing. (The one exception is inside a lambda, where a builtin is reached through the closure's
captured function pointer rather than by name, the same way `say` is; there a
release build keeps a call to an empty function.) Either way it does nothing if
the program is run outside a debugger.

## Looking around

The Tomo-specific commands are:

| Command | What it does |
|---|---|
| `tlocals` | The Tomo variables in scope, printed the way Tomo prints them |
| `p` *expr* | Print, understanding Tomo variable names |
| `tframe` | Re-show where the program is stopped |

`tlocals` shows Tomo values rather than the C representations they compile to,
and inside a lambda it includes the variables the lambda closed over as well as
its arguments:

```
(gdb) tlocals
total = 3
names = ["alice", "bob"]
scores = {"a": 1, "b": 2}
```

Values are printed with Tomo's own formatter, the same output `say()` would
produce, syntax coloring included. gdb's own `print` gets the same treatment
wherever the C type says which Tomo type it is: the number types, `Text`,
`Path`, and every struct and enum the program defines. So `info locals`, a
struct's fields, the value `finish` reports, and a backtrace's arguments all
read as Tomo too.

Lists, tables, and optionals are the exception. Their C type records nothing
about what they hold, so formatting one needs the type information a `--debug`
build puts beside each variable, which is why `tlocals` and `p` can show them
and a bare `print` of the underlying C value cannot.

### Moving around the stack

`bt` is the stack in Tomo's terms: the functions under their Tomo names, with
their arguments rendered the way Tomo writes them:

```
(gdb) bt
#0 Foo.doop(f=Foo{name="widgets", n=7}) at foo.tm:3
#1 helper(label="widgets", count=7, items=[1, 2, 3]) at foo.tm:6
#2 main() at foo.tm:9
#3 cli_handler.main at .tomo/foo.tm.c:50
```

This replaces gdb's `backtrace` (and `bt`/`where`). It shows every frame,
including the runtime C ones in between; a frame that isn't Tomo code gets no
argument list, because there is no Tomo rendering of one to give. Argument
values are cut short (`items=[1, 2, 3, 4, 5, 6, 7, 8,…`), since a frame line is
a summary, and `tlocals` is where a value is read properly. The frames a failure
passed through on its way out of the runtime are shown too, so the Tomo frames
of a crash are usually a few rows down.

`bt` *n* shows the innermost *n* frames and `bt -`*n* the outermost *n*. Any
other argument is handed to gdb's own backtrace, which is still there in full as
`info stack`.

`frame` *n*, `up`, and `down` select a frame and then report it the way a stop
does: the Tomo name of the function, the source around the line, and the
variables in scope. Selecting a frame that is not Tomo code (the runtime, the
generated command-line wrapper) reports nothing extra, because there is no Tomo
view of it to give.

### Exact numbers

A `Num` is exact, so what it prints is exact too, and `32768/3` or
`1/2 + sqrt(5)/2` is the right answer without being a readable one. The
debugger shows the decimal alongside it:

```
(gdb) tlocals
third = 32768/3 ≈ 10922.6666666667
quarter = 0.25
whole = 42
circle = pi ≈ 3.1415926536
root = sqrt(2) ≈ 1.4142135624
golden = 1/2 + sqrt(5)/2 ≈ 1.6180339887
```

A value whose exact form is already a decimal (`0.25`, `42`) is left alone,
since there is nothing to approximate. `set tomo-num-digits` *n* changes how many
fractional digits the approximation carries, `0` turns it off, and
`show tomo-num-digits` reports it. The decimal is correctly rounded, which is
why it is marked `≈` rather than printed as the value: Tomo's own
`Num.digits()` truncates instead, so that every digit it shows is one the value
actually has.

### Long values

A Tomo value is formatted whole, so a list is one line however long it is, and
the debugger prints everything in scope at every stop, so one big value would
otherwise bury the rest of the screen. Values are cut off after `print elements` characters (200 by default):

```
(gdb) p huge
huge = [1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225, …
(cut off; `set print elements unlimited` for the rest)
```

The cutoff is gdb's own `set print elements`, so `set print elements 40` and
`set print elements unlimited` do what you would expect. It applies to
everything that prints a Tomo value, gdb's own `print` included. Cutting happens
on visible characters, so a colored value never loses its closing escape.

### Variable names

A Tomo variable `x` is `_$x` in the generated C. The prefix is what keeps Tomo
names from colliding with C keywords and with the runtime's own symbols, but it
means a bare Tomo name typed at gdb either finds nothing or, worse, finds
something else entirely:

```
(gdb) print x
No symbol "x" in current context.
(gdb) print log
$1 = {<text variable, no debug info>} 0x1211130 <log>     # libm's log()!
```

`p` rewrites Tomo names to the C names they compile to, so those questions get
the answers you meant:

```
(gdb) p x
x = 16384
(gdb) p log
log = @[2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384]
```

`p` replaces gdb's `p`, which is an alias for `print`. `print` itself is
untouched and still takes C names, and anything `p` doesn't recognize is handed
straight to it, so format letters (`p/x n`), value history, and the rest all
behave as they always did. Commands that aren't `p` take the C name, so a
watchpoint on `x` is `watch _$x`.

The rewriting covers names inside larger expressions too, but what gdb
evaluates is still C: a Tomo `Int` is a tagged struct rather than a C integer,
and Tomo's operators and methods are not things gdb can call. `p x` works;
`p x + 1` does not.

## Catching failures

A Tomo runtime error, whether `fail()`, a failed assertion, or an index out of
range, normally prints its report and exits. Under `--debug` it stops in the
debugger instead, after printing that report, with the failing frame and its variables
still intact:

```
Error: index 99 is past the end of [10, 20, 30]

Program received signal SIGABRT, Aborted.

risky (boom.tm:3)
   1│ func risky(xs:[Int], i:Int -> Int)
   2│     if i > xs.length
 > 3│         fail("index $i is past the end of $xs")
   4│     return xs[i]!

   xs = [10, 20, 30]
   i = 99
(gdb) bt
#0 __restore_sigs
#1 raise
#2 fail_text at src/stdlib/fail.c:18
#3 risky(xs=[10, 20, 30], i=99) at boom.tm:3
#4 main() at boom.tm:9
```

`SIGSEGV`, `SIGFPE`, `SIGBUS`, and `SIGSYS` stop the same way. So does
`SIGILL`, which is how a `--debug` build reports undefined behavior: debug
builds compile at `-O0`, which traps it (see **Optimization** below).

`Ctrl-C` interrupts the program and stops it wherever it happens to be, without
killing it; `continue` resumes.

## Optimization

`--debug` compiles at `-O0` so that the program a debugger sees is the program
that was written: every statement is where the source says it is, every
variable exists, and nothing has been folded away.

An explicit `-O` overrides this, since a bug that only appears optimized has to
be debugged optimized:

```bash
$ tomo run --debug -O3 prog.tm
```

Expect the usual optimized-build experience there: variables reported as
optimized out, and lines executing in an order that doesn't match the source.

## Debugging a built program

`--debug` also works on `tomo build`, `tomo transpile`, and `tomo test`. A
program built with it can be started under the debugger by hand:

```bash
$ tomo build --debug prog.tm
$ prefix="$(dirname "$(dirname "$(readlink -f "$(command -v tomo)")")")"
$ gdb -x "$prefix/lib/tomo@$(tomo --version)/tomo-gdb.py" ./prog
```

One thing `tomo run --debug` does that this doesn't: it sets `TOMO_CORE_DUMP`,
which is what turns a runtime error into a stop rather than an exit. Set it
yourself to get the same behavior:

```bash
$ TOMO_CORE_DUMP=yes gdb -x "$prefix/lib/tomo@$(tomo --version)/tomo-gdb.py" ./prog
```

## How it works

Tomo compiles to C, and the generated code carries `#line` directives pointing
back at the `.tm` file (this is `--source-mapping`, on by default). That alone
is enough for gdb to set breakpoints on Tomo lines, step through Tomo source,
and produce backtraces in Tomo terms, with no help from Tomo needed to do any
of that.

What it can't do on its own is show a Tomo *value*. An `Int` is a tagged
small-integer-or-bignum, a `Text` is a rope, and a `[Int]` or `{Text:Int}`
doesn't record in its C type what it holds. Printing those means calling Tomo's
own formatter, `generic_as_text()`, which needs the value's `TypeInfo`. So a
`--debug` build emits one: beside every variable `x` it puts an `x$typeinfo`
holding exactly that, in the same scope. The debugger finds the companion next
to the variable, which is what makes type-erased values printable and gets
shadowing right for free.

The Tomo layer itself is `lib/tomo@`*version*`/tomo-gdb.py` in the
installation, and `tomo run --debug` loads it with `gdb -x`. Set `TOMO_DEBUGGER`
to use a different gdb (or a gdb-compatible debugger) than the one on `$PATH`.

## See also

- [Profiling](profiling.md): `--instrument`, for where a program spends its time
- [Compilation Pipeline](compilation.md): what Tomo generates and how
