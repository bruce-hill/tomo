#!/bin/sh
# Stage a debug-info-free copy of the C runtime that `zig cc` links into
# static (linux/musl) executables: musl libc, compiler-rt, zigc, libunwind,
# and crt1.o. Tomo links compiled programs with -nostdlib against these
# copies (see src/tomo.c) instead of letting zig supply its own, because zig
# unconditionally builds its C runtime with full DWARF -- megabytes of debug
# info for libc internals that tomo stacktraces and gdb sessions never look
# at, baked into every compiled program.
#
# Zig has no flag to build its libc without debug info, but its internal
# strip setting propagates from the link to the libc sub-compilations: a link
# with -Wl,--strip-debug makes zig build musl/compiler-rt/etc. with no DWARF
# at all. So: run one probe link with that flag in a throwaway cache, then
# collect the artifacts zig built. Zig's archives are "thin" (they reference
# object files elsewhere in the cache), so they're flattened into regular
# archives that outlive the cache.
#
# Usage: stage_zig_libc.sh ZIG TARGET_TRIPLE OUTPUT_DIR
set -e

zig="$1"
triple="$2"
out="$3"
if [ -z "$zig" ] || [ -z "$triple" ] || [ -z "$out" ]; then
    echo "Usage: stage_zig_libc.sh ZIG TARGET_TRIPLE OUTPUT_DIR" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cache="$tmp/cache"

# The probe: a minimal static link with the same libraries tomo passes
# (-lunwind, for stacktrace collection), so the cache ends up containing
# exactly one debug-stripped build of each artifact for this target.
echo 'int main(void) { return 0; }' > "$tmp/probe.c"
ZIG_GLOBAL_CACHE_DIR="$cache" ZIG_LOCAL_CACHE_DIR="$cache" \
    "$zig" cc -target "$triple" -O2 -static -lunwind -Wl,--strip-debug \
    "$tmp/probe.c" -o "$tmp/probe"

mkdir -p "$out"

# Copy one artifact out of the cache, flattening thin archives. "optional"
# artifacts are ones a zig version may not produce (libzigc.a is new in zig
# 0.16); everything else missing means zig's layout changed and this script
# needs updating.
stage() { # stage NAME [optional]
    src=$(find "$cache" -name "$1" | head -n 1)
    if [ -z "$src" ]; then
        if [ "$2" = optional ]; then return 0; fi
        echo "stage_zig_libc.sh: no $1 in the probe link's cache (did zig's libc layout change?)" >&2
        exit 1
    fi
    rm -f "$out/$1"
    if [ "$(head -c 7 "$src")" = '!<thin>' ]; then
        # `q` (append, never replace) tolerates duplicate member basenames;
        # the member list goes through a response file to dodge ARG_MAX:
        "$zig" ar t "$src" | sed 's/.*/"&"/' > "$tmp/members"
        "$zig" ar qcs "$out/$1" "@$tmp/members"
    else
        cp "$src" "$out/$1"
    fi
}

stage libc.a
stage libcompiler_rt.a
stage libzigc.a optional
stage libunwind.a
stage crt1.o
