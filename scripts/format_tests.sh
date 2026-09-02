#!/bin/sh
# Snapshot tests for the formatter. Each test/format/*.tm is run through
# `tomo format` and the result compared against a snapshot checked in beside it
# (foo.tm -> foo.tm.formatted).
#
# This is a different question from the one `tomo format --check` asks. That
# checks formatting is *faithful*: the result reparses to the same syntax tree
# and formatting it again changes nothing. Layout it cannot see, because layout
# is exactly what a formatter is allowed to change -- and because reparsing
# dedents a verbatim block, even inline C that grows from one line to three
# passes it. These snapshots pin the layout the formatter actually produces.
#
# Usage: format_tests.sh TOMO [--regen]
#
# --regen rewrites every snapshot from current behavior. It can't tell a fixed
# bug from a newly introduced one, so review `git diff test/format` afterwards.
set -e

tomo=$1
if [ -z "$tomo" ]; then
    echo "usage: $0 TOMO [--regen]" >&2
    exit 1
fi
regen=$2

# Pin everything the output depends on, as the parser snapshots do:
COLOR=0
LC_ALL=C
export COLOR LC_ALL
unset TOMO_STACKTRACE

repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
cd "$repo"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checked=0
failed=0
for source in test/format/*.tm; do
    [ -e "$source" ] || continue
    checked=$((checked + 1))
    snapshot="$source.formatted"
    actual="$tmp/$(basename "$source").formatted"

    "$tomo" format "$source" >"$actual" 2>&1

    if [ "$regen" = "--regen" ]; then
        cp "$actual" "$snapshot"
        continue
    fi

    if [ ! -e "$snapshot" ]; then
        printf '\033[31;1mno snapshot\033[m: %s\n' "$snapshot" >&2
        printf '  (run `make regen-format-tests` to create it)\n' >&2
        failed=$((failed + 1))
    elif ! diff -u "$snapshot" "$actual" > "$tmp/diff"; then
        printf '\033[31;1mmismatch\033[m: %s\n' "$source" >&2
        sed 's/^/  /' "$tmp/diff" >&2
        failed=$((failed + 1))
    fi
done

if [ "$regen" = "--regen" ]; then
    printf 'Regenerated %s formatter snapshot(s). Review `git diff test/format`.\n' "$checked"
    exit 0
fi

# A silent pass would mean the fixtures had gone missing, not that they passed:
if [ "$checked" -eq 0 ]; then
    echo "No formatter test fixtures found in test/format/" >&2
    exit 1
fi

if [ "$failed" -gt 0 ]; then
    printf '\033[31;1m%s of %s formatter snapshots differ.\033[m\n' "$failed" "$checked" >&2
    exit 1
fi
printf 'All %s formatter snapshots matched.\n' "$checked"
