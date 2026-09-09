#!/usr/bin/env python3
"""Round-trip tests for the formatter over generated source.

Every case is formatted and the result checked with `tomo format --verify`:
it has to reparse to the same syntax tree, and formatting it again has to
change nothing. This is the check that catches a formatter turning working
source into source that doesn't parse, or into source that parses as
something else -- both of which it has done.

What the shapes below are for is coverage of the *positions* a construct can
be written in, which is where those bugs live. A comprehension formatted
correctly on its own came back wrong beside another list item; a signature
that fit came back over-long once its return type was counted. So each
section crosses one construct against the contexts around it, rather than
listing interesting values.

What it does not check is layout: --verify cannot see line width, and width
is exactly what a formatter is allowed to change. The snapshots in
test/format/ pin that; this pins that the result still means what it said.

Cases whose source doesn't parse are dropped and counted -- a generator that
writes nonsense should say how much, not quietly shrink the suite. The whole
comprehension section was silently invalid the first time this ran, and the
count is what said so.

Usage: format_fuzz.py TOMO [--keep DIR]
"""

import itertools
import os
import subprocess
import sys
import tempfile
import time

# A comprehension written bare runs on, so what may follow it decides whether
# it keeps its parentheses. These are the positions one can be written in.
COMPREHENSIONS = ["x for x in xs", "x*2 for x in xs if x > 0", "k for k, v in table",
                  "x for x at i in xs", "x for x in xs, ys"]
COMPREHENSION_CONTEXTS = [
    "a := ({c})", "_ := take(({c}))", ">> ({c})", "a := [({c})]", "a := [({c}), 1]",
    "a := [1, ({c})]", "a := [({c}), ({c})]", "a := [{c}]", "a := {{({c})}}",
    "a := (+: {c})", "a := (+: ({c}))", "a := [(({c})) for y in xs]",
    "a := {{1: 2, ({c}): 3}}", "_ := take(({c})) + take(({c}))",
]

# A definition's fields, its flags, and the namespace body that can follow.
FIELD_COUNTS = [0, 1, 3, 12]
TYPE_FLAGS = ["", "; secret", "; packed_bools", "; secret; packed_bools"]

# A signature's arguments, return type and flags each take part of the line,
# and each has to survive the list above it wrapping.
ARG_COUNTS = [0, 1, 9]
RETURN_TYPES = ["", " -> Int", " -> {Text:[Int]}", " -> func(x:Int -> Text)"]
FUNC_FLAGS = ["", "; inline", "; cached", "; cache_size=100", "; inline; cached",
              "; cache_size=100; inline"]

# Operators, whose spacing and parenthesization depend on what they sit among.
OPERANDS = ["1", "1.", "x", "-2", "(a + b)", "f(x)", "2..power(3)", "xs[1]", "0.5"]
OPERATORS = ["+", "-", "*", "/", "//", "mod", "^", "<<", ">>", "and", "or", "xor",
             "_min_", "_max_", "==", "<", "++"]


def comprehension_cases():
    for comp, form in itertools.product(COMPREHENSIONS, COMPREHENSION_CONTEXTS):
        yield ("func take(g:func(->Int?) -> Int)\n    return 0\n"
               "func main()\n    xs := [1]\n    ys := [2]\n    table := {1: 2}\n    "
               + form.replace("{c}", comp) + "\n    pass\n")


def definition_cases():
    for count, flags in itertools.product(FIELD_COUNTS, TYPE_FLAGS):
        fields = ", ".join(f"field_number_{i}:Bool" for i in range(1, count + 1))
        tags = ", ".join([f"Tag{i}{{{fields}{flags}}}" for i in range(1, 4)] + ["Bare"])
        yield (f"struct S{{{fields}{flags}}}\n"
               f"struct WithBody{{{fields}{flags}}}\n"
               f"    func get(s:WithBody -> Int)\n        return 1\n"
               f"enum E({tags})\n")


def signature_cases():
    for count, ret, flags in itertools.product(ARG_COUNTS, RETURN_TYPES, FUNC_FLAGS):
        args = ", ".join(f"argument_number_{i}:Int" for i in range(1, count + 1))
        body = "    return 1\n" if ret == " -> Int" else "    pass\n"
        lambda_args = args if args else ""
        yield (f"func f({args}{ret}{flags})\n{body}"
               f"func g()\n    h := func({lambda_args}{ret}) 1\n    pass\n")


def operator_cases():
    for lhs, op, rhs in itertools.product(OPERANDS, OPERATORS, ["y", "2", "(c + d)"]):
        expr = f"{lhs} {op} {rhs}"
        yield ("func main()\n    x := 1\n    y := 2\n    a := 3\n    b := 4\n    c := 5\n"
               "    d := 6\n    xs := [1]\n"
               f"    >> {expr}\n    >> ({expr}) * 2\n    >> 1 + {expr}\n    >> [{expr}]\n")


SECTIONS = [("comprehension", comprehension_cases), ("definition", definition_cases),
            ("signature", signature_cases), ("operator", operator_cases)]


BATCH = 40  # Cases per `--verify` call, so a section reports as it goes.


def render(label, total, elapsed=0.0, failed=0, dropped=0, done=None):
    """One section's line, mid-run if `done` is given and finished otherwise."""
    dots = "." * max(1, 44 - len(label))
    if done is not None:
        return f"  {label} {dots} {done}/{total}"
    body = f"{total:>4} cases {elapsed:5.1f}s"
    if dropped:
        body += f"  {dropped} dropped"
    if failed:
        # The colour goes after the dots, which pad by width and can't count it.
        return f"  {label} {dots} \033[31;1m{body}  {failed} failed\033[m"
    return f"  {label} {dots} {body}"


def verify(tomo, paths, env):
    """The names among these whose formatting isn't faithful.

    --verify writes its paths relative to the repository, so they are matched
    back to the ones handed in by name rather than used as given.
    """
    if not paths:
        return []
    result = subprocess.run([tomo, "format", "--verify"] + paths, env=env,
                            capture_output=True, text=True)
    failed = {os.path.basename(line.split()[1])
              for line in result.stdout.splitlines() if line.startswith("FAIL")}
    return [p for p in paths if os.path.basename(p) in failed]


def main():
    if len(sys.argv) < 2:
        sys.exit(f"usage: {sys.argv[0]} TOMO [--keep DIR]")
    tomo = os.path.abspath(sys.argv[1])
    keep = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--keep" else None
    env = dict(os.environ, COLOR="0", LC_ALL="C")
    env.pop("TOMO_STACKTRACE", None)
    tty = sys.stdout.isatty()

    total_cases = total_dropped = 0
    broken = []  # (name, source) -- the temporary directory is gone by the time
                 # these are reported, so the text travels with the name.
    formatted = None  # What the first failing case came out as.
    with tempfile.TemporaryDirectory() as tmp:
        out = keep or tmp
        os.makedirs(out, exist_ok=True)
        for label, generate in SECTIONS:
            paths, sources = [], {}
            for i, source in enumerate(generate()):
                path = os.path.join(out, f"{label}_{i:04d}.tm")
                with open(path, "w") as f:
                    f.write(source)
                paths.append(path)
                sources[path] = source

            started = time.monotonic()
            failures = []
            for at in range(0, len(paths), BATCH):
                if tty:
                    print(render(label, len(paths), done=at), end="\r", flush=True)
                failures += verify(tomo, paths[at:at + BATCH], env)
            # A shape that isn't valid Tomo says nothing about the formatter, so
            # it is dropped rather than counted as a pass -- but it is counted,
            # because a generator writing nonsense shrinks the suite silently.
            dropped = [p for p in failures
                       if subprocess.run([tomo, "parse", p], env=env,
                                         capture_output=True).returncode != 0]
            failures = [p for p in failures if p not in dropped]
            elapsed = time.monotonic() - started

            if tty:
                print(" " * 78, end="\r")
            print(render(label, len(paths) - len(dropped), elapsed, len(failures),
                         len(dropped)), flush=True)
            total_cases += len(paths) - len(dropped)
            total_dropped += len(dropped)
            if failures and formatted is None:
                formatted = subprocess.run([tomo, "format", failures[0]], env=env,
                                           capture_output=True, text=True).stdout
            broken += [(os.path.basename(p), sources[p]) for p in failures]

    if broken:
        # A formatter that breaks one shape usually breaks a hundred of them,
        # so the sources are worth showing but not all of them.
        for i, (name, source) in enumerate(broken[:10]):
            print(f"\n  did not round-trip: {name}", file=sys.stderr)
            for line in source.splitlines():
                print(f"      {line}", file=sys.stderr)
            # What it turned into, for the first one only: that is what has to
            # be read to know whether the tree moved or the result won't parse,
            # and ten of them would bury the list of what else broke.
            if i == 0 and formatted:
                print("    formatted to:", file=sys.stderr)
                for line in formatted.splitlines():
                    print(f"      {line}", file=sys.stderr)
        if len(broken) > 10:
            print(f"\n  ...and {len(broken) - 10} more", file=sys.stderr)
        print(f"\033[31;1m{len(broken)} of {total_cases} generated cases did not "
              f"round-trip.\033[m", file=sys.stderr)
        if keep:
            print(f"cases kept in {keep}", file=sys.stderr)
        sys.exit(1)

    # A silent pass would mean the generators had stopped generating:
    if total_cases == 0:
        print("No cases were generated.", file=sys.stderr)
        sys.exit(1)
    dropped_note = f" ({total_dropped} dropped as invalid Tomo)" if total_dropped else ""
    print(f"All {total_cases} generated cases round-tripped{dropped_note}.")


if __name__ == "__main__":
    main()
