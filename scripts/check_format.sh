#!/bin/sh
# Check that `tomo fmt` is faithful on every Tomo file passed in (or on the
# whole in-tree corpus, if none are). For each file:
#
#   * it formats,
#   * the formatted source parses,
#   * it parses to the same syntax tree as the original, and
#   * formatting it again changes nothing.
#
# Any of those failing means the formatter rewrote a program into something
# different, something unparseable, or something it doesn't consider settled.
set -eu

repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
tomo="$repo/local-tomo"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [ "$#" -gt 0 ]; then
    files="$*"
else
    files="$(find "$repo/test" "$repo/examples" "$repo/benchmarks" -name '*.tm' 2>/dev/null | sort)"
fi

failures=0
for f in $files; do
    # Anonymous types are named after their byte offset in the file, which
    # reformatting legitimately moves, so compare with those names elided:
    if ! before="$("$tomo" parse "$f" 2>"$tmp/err")"; then
        printf '\033[31;1mparse failed\033[m: %s\n' "$f"
        sed 's/^/    /' "$tmp/err"
        failures=$((failures + 1))
        continue
    fi
    if ! "$tomo" fmt "$f" >"$tmp/once" 2>"$tmp/err"; then
        printf '\033[31;1mfmt failed\033[m: %s\n' "$f"
        sed 's/^/    /' "$tmp/err"
        failures=$((failures + 1))
        continue
    fi
    if ! after="$("$tomo" parse "$tmp/once" 2>"$tmp/err")"; then
        printf "\033[31;1mformatted source doesn't parse\033[m: %s\n" "$f"
        sed 's/^/    /' "$tmp/err"
        failures=$((failures + 1))
        continue
    fi
    if [ "$(printf '%s' "$before" | sed 's/\$[0-9][0-9]*/$/g')" \
         != "$(printf '%s' "$after" | sed 's/\$[0-9][0-9]*/$/g')" ]; then
        printf '\033[31;1mformatting changed the syntax tree\033[m: %s\n' "$f"
        failures=$((failures + 1))
        continue
    fi
    "$tomo" fmt "$tmp/once" >"$tmp/twice" 2>/dev/null || true
    if ! cmp -s "$tmp/once" "$tmp/twice"; then
        printf '\033[31;1mformatting is not idempotent\033[m: %s\n' "$f"
        diff "$tmp/once" "$tmp/twice" | head -20 | sed 's/^/    /'
        failures=$((failures + 1))
    fi
done

if [ "$failures" -gt 0 ]; then
    printf '\033[31;7m %d FORMATTING FAILURE(S) \033[m\n' "$failures"
    exit 1
fi
printf '\033[92;1m ✅ formatting is faithful on every file\033[m\n'
