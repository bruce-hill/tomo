# fannkuch-redux — The Computer Language Benchmarks Game
#
# A Tomo port. Pure Tomo; no inline C — the algorithm and all its output
# formatting use plain Tomo integers and `say()`.
#
# Design notes:
#   - `perm`, `perm1`, and `count` are mutable `@[Int64]` arrays, indexed
#     like the reference's 0-indexed C arrays shifted by one (Tomo lists are
#     1-indexed, so a literal C index `i` becomes Tomo index `i+1`). The
#     reference's own indexing isn't uniform, though: in the top "reset"
#     loop it writes `count[r-1]` (Tomo: `count[r]`), but in the "generate
#     next permutation" section it reads/writes `count[r]`/`perm1[r]`
#     directly (Tomo: `count[r+1]`/`perm1[r+1]`) and shifts with `i < r`
#     (Tomo: `i <= r`) — same `r`, different offset depending on which part
#     of the loop last touched it. Ported by tracking each access back to
#     the literal C index it corresponds to, not by applying one blanket
#     `+1` rule.
#   - Indexed reads are unwrapped with `!` (bounds-checked, per-element);
#     indexed writes are plain assignment, since compound assignment through
#     an index (`count[r] -= 1`) isn't supported.
#   - Hot loops iterate `Int64(1).to(k2)`, NOT `1.to(k2)`: a bare `1` literal
#     is a default arbitrary-precision `Int`, so `1.to(k2)` yields a *bignum*
#     range whose counter and index math compile to tagged `Int$plus` /
#     `Int$compare_value` calls (plus an `Int64$from_int` per access). Making
#     the range's start an `Int64` keeps the counter native — a 2.6× speedup
#     on the flip loop here. The comprehensions above run once at setup, so
#     their bignum counters don't matter and stay as plain `1.to(n)`.
#   - The permutation-count parity check uses `mod`, not C's `%`, since
#     Tomo's `mod` is always non-negative (irrelevant here since both
#     operands are non-negative, but it's the idiomatic operator).
#
# Usage: fannkuchredux <n>   (e.g. ./fannkuchredux 12)

func fannkuchredux(n:Int64 -> Int64)
    perm := @[Int64(0) for _ in 1.to(n)]
    perm1 := @[Int64(k-1) for k in 1.to(n)]
    count := @[Int64(0) for _ in 1.to(n)]
    max_flips := Int64(0)
    perm_count := Int64(0)
    checksum := Int64(0)
    r := n

    repeat
        while r != 1
            count[r] = r
            r -= 1

        for i in Int64(1).to(n)
            perm[i] = perm1[i]!

        flips := Int64(0)
        while perm[1]! != 0
            k := perm[1]!
            k2 := (k+1)/2
            for i in Int64(1).to(k2)
                tmp := perm[i]!
                perm[i] = perm[k-i+2]!
                perm[k-i+2] = tmp
            flips += 1

        if flips > max_flips
            max_flips = flips
        if perm_count mod 2 == 0
            checksum += flips
        else
            checksum -= flips

        # Use incremental change to generate the next permutation:
        repeat
            if r == n
                say("$checksum")
                return max_flips

            perm0 := perm1[1]!
            i := Int64(1)
            while i <= r
                j := i + 1
                perm1[i] = perm1[j]!
                i = j
            perm1[r+1] = perm0
            count[r+1] = count[r+1]! - 1
            if count[r+1]! > 0
                stop
            r += 1
        perm_count += 1

    fail("Unreachable")

func main(n:Int64)
    max_flips := fannkuchredux(n)
    say("Pfannkuchen($n) = $max_flips")
