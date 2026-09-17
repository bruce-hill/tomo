#!/bin/sh
# End-to-end tests for `tomo eval`. Each case is an expression and the exact
# output it should produce, checked by running the real command, since what
# eval does (writing a scratch file, compiling it so every top-level statement
# prints what it evaluates to, running it) reaches across the compiler and is
# not covered by the .tm test suite.
#
# Usage: eval_tests.sh TOMO
set -e

tomo=$1
if [ -z "$tomo" ]; then
    echo "usage: $0 TOMO" >&2
    exit 1
fi

# Values are colorized only when stdout is a terminal, which it isn't here, but
# a developer's COLOR=1 would override that and bake escapes into the output:
COLOR=0
LC_ALL=C
export COLOR LC_ALL

repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
cd "$repo"

checked=0
failed=0

# check EXPRESSION EXPECTED_OUTPUT
check() {
    checked=$((checked + 1))
    actual="$("$tomo" eval "$1" 2>&1)" || true
    if [ "$actual" = "$2" ]; then
        printf '  \033[32mok\033[m %s\n' "$(printf '%s' "$1" | tr '\n' ' ')"
    else
        failed=$((failed + 1))
        printf '  \033[31mFAIL\033[m %s\n' "$(printf '%s' "$1" | tr '\n' ' ')"
        printf '    expected: %s\n' "$(printf '%s' "$2" | tr '\n' '|')"
        printf '      actual: %s\n' "$(printf '%s' "$actual" | tr '\n' '|')"
    fi
}

check '1+2' '3'
# Text prints the way `>>` prints it, quotes included:
check '"hello"' '"hello"'
check '[1, 2, 3]' '[1, 2, 3]'
# Every statement that evaluates to something prints, whether the statements
# are separated by newlines or by `;`:
check '1; 2; 3' '1
2
3'
check '1
2' '1
2'
# A declaration evaluates to nothing, and neither does a call that returns
# nothing, so neither prints anything of its own:
check 'x := 5; x * 2' '10'
check 'say("hi")' 'hi'
# The argument is ordinary Tomo source, so it can define things:
check 'func triple(x:Int -> Int)
    return x * 3
triple(14)' '42'

if [ "$failed" -gt 0 ]; then
    printf '\033[31;1m%d of %d eval tests failed\033[m\n' "$failed" "$checked"
    exit 1
fi
printf 'All %d eval tests passed.\n' "$checked"
