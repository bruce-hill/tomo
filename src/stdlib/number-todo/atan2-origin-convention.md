# Revisit atan2(0, 0): error vs. C's 0

`number_atan2(0, 0)` currently returns the error value
(`ERR_ATAN2_ORIGIN`, "the angle is undefined"), because the direction at the
origin genuinely is undefined and that matches this library's convention of
returning the sticky error value for domain violations (`ln(0)`, `tan(pi/2)`,
`sqrt(-1)`, ...) rather than papering over them the way IEEE NaN does.

C's `atan2(0, 0)`, by contrast, returns `+0` (well-defined by the standard).
So this is a deliberate divergence — the one place `number_atan2` does not
match libm.

## The decision

Two defensible positions, and which is right depends on the priority:

- **Keep error** (current): consistent with the library's "undefined result
  is an error, detectable via `number_is_error`" ethos. Correct-math-first.
- **Return 0** to match libm exactly: better fidelity for the "drop-in
  replacement for `double`" goal, where ported code may rely on
  `atan2(0,0) == 0` not trapping.

If it flips to 0, it's a ~one-line change in `number_atan2` (drop the
`ERR_ATAN2_ORIGIN` early-return, let the `sx == 0` path fall through — note
`sy == 0` there must then be handled to yield 0, not `±pi/2`), plus the
test in test/c/number_test.c and the doc comments in number.h / number.c, and
`ERR_ATAN2_ORIGIN` becomes unused.

## Verify

- test/c/number_test.c already pins `atan2(0,0)` → error; that assertion is the
  thing to flip if the decision changes.

## Depends on

Nothing. This is a product decision, not a technical blocker.
