# fannkuch-redux — The Computer Language Benchmarks Game
#
# A Tomo port of Oleg Mazurov's algorithm (the one used by the fastest Go,
# Java, and C++ entries), single-threaded: the reference programs use its
# factorial-base indexing only to split the permutation space across threads,
# which Tomo doesn't have, so the chunking machinery drops out entirely and
# permutations are generated incrementally from the identity.
#
# What makes it faster than the naive reference algorithm:
#   - Flips are counted on a scratch copy (`pp`) with early-outs: a
#     permutation starting with 0 counts no flips, and one whose first flip
#     ends the game counts 1 — neither needs any copying at all.
#   - Each flip reverses only the *interior* of the prefix; the two ends are
#     exchanged implicitly by tracking the virtual first element (`p0`) in a
#     local, halving the swaps per flip.
#   - Permutation advance alternates parity: every other step is a single
#     `p.swap(1, 2)` instead of a rotate, and no step copies the whole array.
#
# Tomo-specific notes (see nbody.tm and git history for the general levers):
#   - Pure Tomo; no inline C anywhere — output uses `say()`.
#   - Hot loops iterate `Int64(...)` ranges so counters stay native, and use
#     `p.swap(i, j)`/indexed access on `&[Int64]` lists (stack-allocated
#     headers; the list values never escape). The main loop body
#     qualifies for the compiler's CoW-guard/header hoisting (no calls except
#     swap/constructors), so element accesses compile against hoisted
#     data/stride/length locals with no per-access CoW checks.
#   - `p` stores 0-based *values* (matching the algorithm's arithmetic);
#     list *indexes* are Tomo's 1-based, hence the `+1` on value-as-index
#     accesses like `p[first+1]`.
#
# Usage: fannkuchredux <n>   (e.g. ./fannkuchredux 12)

func fannkuchredux(n:Int64 -> Int64)
    p := &[Int64(k-1) for k in 1.to(n)]
    pp := &[Int64(0) for _ in 1.to(n)]
    count := &[Int64(0) for _ in 1.to(n)]
    max_flips := Int64(0)
    checksum := Int64(0)

    total := Int64(1)
    for i in Int64(1).to(n)
        total *= i

    idx := Int64(0)
    sign := yes
    first := Int64(0)
    repeat
        # Count the flips for the current permutation:
        first = p[1]!
        if first != 0
            flips := Int64(1)
            if p[first+1]! != 0
                for i in Int64(1).to(n)
                    pp[i] = p[i]!
                p0 := first
                repeat
                    flips += 1
                    # Reverse the interior of the prefix [1 .. p0+1]; the two
                    # ends are exchanged via the `p0` local instead of memory.
                    i := Int64(2)
                    j := p0
                    while i < j
                        pp.swap(i, j)
                        i += 1
                        j -= 1
                    t := pp[p0+1]!
                    pp[p0+1] = p0
                    p0 = t
                    if pp[p0+1]! == 0
                        break
            if flips > max_flips
                max_flips = flips
            if sign
                checksum += flips
            else
                checksum -= flips

        idx += 1
        if idx == total
            break

        # Advance to the next permutation:
        if sign
            p.swap(1, 2)
        else
            p.swap(2, 3)
            k := Int64(3)
            repeat
                count[k] = count[k]! + 1
                if count[k]! < k
                    break
                count[k] = 0
                # Rotate the prefix [1 .. k+1] left by one:
                for j in Int64(1).to(k)
                    p[j] = p[j+1]!
                p[k+1] = first
                first = p[1]!
                k += 1
        sign = not sign

    say("$checksum")
    return max_flips

func main(n:Int64)
    max_flips := fannkuchredux(n)
    say("Pfannkuchen($n) = $max_flips")
