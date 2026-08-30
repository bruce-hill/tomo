# Parser tests

Snapshot tests for the parser. Each `foo.tm` here is parsed by `tomo parse`,
and its combined stdout and stderr is compared against `foo.tm.parse`.

A snapshot holds one of two things, and the runner doesn't distinguish them:

- for a well-formed fixture, the parse tree as an S-expression;
- for an `err_*.tm` fixture, which is malformed on purpose, the full text of
  the parse error.

Because the two are interchangeable, a file that unexpectedly stops parsing,
or one that unexpectedly starts, fails against its snapshot either way. The
error snapshots also pin the caret span, which nothing else tests: a caret that
drifts to the wrong characters breaks no other test in the suite.

Run them with `make test-parse`. After a deliberate change to the parser or to
a diagnostic, `make regen-parse-tests` rewrites every snapshot from current
behavior. It cannot tell a fixed bug from a newly introduced one, so read
`git diff test/parse` before committing the result.

These files are deliberately left out of `make test-format`: the `err_*.tm`
fixtures don't parse, so the formatter can't round-trip them.
