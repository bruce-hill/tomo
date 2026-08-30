#!/bin/sh
# Snapshot tests for the parser. Each test/parse/*.tm is run through
# `tomo parse` and its combined stdout and stderr compared against a snapshot
# checked in beside it (foo.tm -> foo.tm.parse).
#
# A snapshot holds either the file's parse tree as an S-expression or, for the
# deliberately malformed `err_*.tm` fixtures, the full text of the parse error.
# The runner doesn't care which: a file that unexpectedly stops parsing, or one
# that unexpectedly starts, fails against its snapshot either way. Error
# snapshots pin the caret span as well as the message, which is otherwise
# untested, since a caret that drifts to the wrong characters breaks nothing that
# any other test can see.
#
# Usage: parse_tests.sh TOMO [--regen]
#
# --regen rewrites every snapshot from current behavior. It can't tell a fixed
# bug from a newly introduced one, so review `git diff test/parse` afterwards.
set -e

tomo=$1
if [ -z "$tomo" ]; then
    echo "usage: $0 TOMO [--regen]" >&2
    exit 1
fi
regen=$2

# Pin everything the output depends on. COLOR=1 in a developer's environment
# would bake ANSI escapes into the snapshots, and TOMO_STACKTRACE has to be
# *unset* rather than empty, since the parser only checks whether it is set:
COLOR=0
LC_ALL=C
export COLOR LC_ALL
unset TOMO_STACKTRACE

# Diagnostics name the source file relative to the working directory, so the
# snapshots only reproduce when that directory is the repository root:
repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
cd "$repo"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checked=0
failed=0
for source in test/parse/*.tm; do
    [ -e "$source" ] || continue
    checked=$((checked + 1))
    snapshot="$source.parse"
    actual="$tmp/$(basename "$source").parse"

    # A parse error exits nonzero, and for the err_* fixtures that's the
    # expected outcome, so the status is ignored in favor of the output:
    "$tomo" parse "$source" >"$actual" 2>&1 || true

    if [ "$regen" = "--regen" ]; then
        cp "$actual" "$snapshot"
        continue
    fi

    if [ ! -e "$snapshot" ]; then
        printf '\033[31;1mno snapshot\033[m: %s\n' "$snapshot" >&2
        printf '  (run `make regen-parse-tests` to create it)\n' >&2
        failed=$((failed + 1))
    elif ! diff -u "$snapshot" "$actual" > "$tmp/diff"; then
        printf '\033[31;1mmismatch\033[m: %s\n' "$source" >&2
        sed 's/^/  /' "$tmp/diff" >&2
        failed=$((failed + 1))
    fi
done

if [ "$regen" = "--regen" ]; then
    printf 'Regenerated %s parser snapshot(s). Review `git diff test/parse`.\n' "$checked"
    exit 0
fi

# A silent pass would mean the fixtures had gone missing, not that they passed:
if [ "$checked" -eq 0 ]; then
    echo "No parser test fixtures found in test/parse/" >&2
    exit 1
fi

if [ "$failed" -gt 0 ]; then
    printf '\033[31;1m%s of %s parser snapshots differ.\033[m\n' "$failed" "$checked" >&2
    exit 1
fi
printf 'All %s parser snapshots matched.\n' "$checked"
