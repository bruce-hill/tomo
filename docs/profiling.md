# Profiling

Compiling or running a program with `--instrument` adds a small timer around
every function it defines. The resulting program profiles itself on every run
and prints a breakdown to stderr when it exits:

```bash
$ tomo build --instrument fib.tm
$ ./fib
6765
───── profile ─────
   self%      self     total      calls   avg self  function
  85.64%   10.08ms   10.08ms          1    10.08ms  slow_sum (fib.tm:5)
   9.73%    1.15ms    1.15ms      21891       52ns  fib (fib.tm:1)
   0.15%    0.02ms   11.25ms          1    17.16us  main (fib.tm:11)
───────────────────
3 functions, 21893 calls, 11.25ms in instrumented code, 11.77ms total runtime
```

You can also do `tomo run --instrument prog.tm` to run with profiling directly,
although the default optimization level for `tomo run` is `-O 1`, so you may
also want to do `tomo run -O 3 --instrument prog.tm` for a more representative
result.

## Reading the report

Rows are sorted by self time, and each one is a function, a conversion, or a
lambda (lambdas have no name of their own, so they are listed as `lambda` and
identified by their source location):

- **self** is the time spent inside the function itself, with the time its
  callees took subtracted out. This is the column that says where the program's
  time actually goes, and what the rows are sorted by.
- **total** is the time spent in the function and everything it called. For a
  recursive function, a call that happens inside another call of the same
  function isn't counted again, so `fib`'s total is the time of the outermost
  calls, not the sum of all 21891 of them.
- **calls** is an exact count, not a sample.
- **self%** is self time as a share of the whole run, including the time before
  and after any instrumented function is on the stack (startup, the runtime,
  and any C code the program uses).

Only functions that actually ran are listed, and time spent inside the standard
library is charged to the Tomo function that called it, since the standard
library isn't instrumented.

If the program exits from inside a function, whether an explicit exit, a
`fail()`, or a crash, the report is still printed, and the functions it died inside are
credited with the time they had used. A run where no instrumented function ran
at all (`--help`, or a rejected argument) prints nothing.

## Flame graphs

Setting the `FLAME_GRAPH` environment variable to a path writes the call tree
there as an SVG flame graph: every frame gets a box as wide as its share of the
run, stacked on the frame that called it, with its name, time, call count, share
of the run, and source location in a hover tooltip.

```bash
$ FLAME_GRAPH=prof.svg ./prog
Wrote flame graph to prof.svg
```

You can view the SVG file in any browser. Unlike the table, the graph is per
*call path*: a function called from two places gets a box under each caller. A
gap above a box is time the function spent in itself rather than in any callee.
Recursive calls collapse into the box they recurse from, so a function that
calls itself a thousand times deep is one box, not a thousand rows.

## Output options

An instrumented program enables profiling by default and outputs to `stderr`,
but that behavior can be controlled by the following environment variables:

```bash
$ PROFILE=0 ./fib                  # run without profiling and print no report
$ PROFILE_FILE=prof.txt ./fib      # write the table to prof.txt instead of stderr
$ PROFILE_FILE=- ./fib             # write it to stdout
$ FLAME_GRAPH=prof.svg ./fib       # also write an SVG flame graph to prof.svg
```

## Performance overhead

Profiling instrumentation is only added when the `--instrument` flag is passed
to the Tomo compiler, so there is zero overhead when the flag is not used. In
an instrumented build:

- With `PROFILE=0`, each call pays one predictable branch. The measurable cost
  is the optimization the instrumentation blocks (the compiler can no longer
  inline or tail-call an instrumented function), which on a call-bound program
  like naive `fib` is around 13% overhead.
- While profiling, each call costs about 13ns: two reads of the CPU's cycle
  counter and a handful of stores into the call tree. Everything the report
  shows is summed out of that tree at exit, so however many times a function
  runs, a call only ever updates its own node. That cost is charged to the
  calling function's self time, so profiles of very hot, very small functions
  still read as somewhat more expensive than they are.

Timestamps come from `rdtsc` on x86 or the architected counter on ARM, falling
back to `clock_gettime()` elsewhere, which costs roughly four times as much per
call.
