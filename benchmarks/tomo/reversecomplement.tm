# reverse-complement — The Computer Language Benchmarks Game
#
# A Tomo port of the gcc-4 reference (single-threaded). Reads FASTA on stdin;
# for each record it prints the header unchanged, then the reverse-complement
# of the sequence, re-wrapped to 60 columns and upper-cased.
#
# Design notes:
#   - The whole input is slurped once as raw `[Byte]` (`read_bytes()`), and all
#     work is byte-level: no Text decoding, no per-line allocation.
#   - `comp` is a 256-entry byte table mapping each nucleotide (upper OR lower
#     case) to its upper-case complement, and everything else to 0.
#   - Header/record boundaries are located with `.find()` (memchr-backed,
#     vectorized), the same technique the fast reference implementations use.
#   - The reverse-complement of each record's sequence is a single list
#     comprehension: `[comp[d] for d in region.reversed() if d != newline]`.
#     `region.reversed()` is an O(1) negative-stride view, and because the
#     comprehension's source is a list of known length, the compiler
#     pre-sizes the result buffer and fills it with inlined appends (no
#     growth reallocation) — so this reads as ordinary idiomatic Tomo yet
#     runs as a tight loop.
#   - That clean, newline-free sequence is then re-wrapped to 60 columns into
#     a reused output buffer (`data`'s own bytes as scratch: one bulk
#     copy-on-write compact on first write, then plain indexed stores) and
#     written per record through a raw `byte_writer` (like fasta).
#
# Usage: reversecomplement < fasta_input   (stdin is FASTA text)

# Build the complement lookup: index by byte value + 1 (lists are 1-indexed).
# Each nucleotide in `letters` maps to the complement at the same position in
# `comps` (A<->T, C<->G, plus the IUB ambiguity codes); all ASCII, so
# `.utf8()` yields exactly their byte values.
func build_comp(-> [Byte])
    letters := "ABCDGHKMNRSTVWY".utf8()
    comps := "TVGHCDMKNYSABWR".utf8()
    comp := &[Byte(0) for _ in 256]
    for i in letters.length
        u := Int64(letters[i]!)
        c := comps[i]!
        comp[u + 1] = c       # upper-case input
        comp[u + 32 + 1] = c  # lower-case input (byte + 32)
    return comp[]

func main()
    data := (/dev/stdin).read_bytes()!
    n := Int64(data.length)
    comp := build_comp()
    emit := (/dev/stdout).byte_writer(append=yes)

    # Reused 60-column-wrapped output buffer (never longer than the input plus
    # one newline per 60 columns). Sliced from `data`, so the first indexed
    # write triggers one bulk copy-on-write compact, not a per-byte cost.
    out := &(data ++ [Byte(0) for _ in n / 60 + 64])
    p := Int64(0)  # write cursor into `out`, shared across all records

    i := Int64(1)
    while i <= n
        # Header line: '>' up to (not including) the newline.
        nl := data.from(i).find(Byte(10))
        header_end := if nl then i + Int64(nl) - 1 else n + 1
        emit(data.slice(i, header_end - 1))!
        emit([Byte(10)])!
        i = header_end + 1

        # Sequence region: up to the next '>' (or EOF).
        seq_start := i
        if i <= n
            gt := data.from(i).find(Byte(62))
            i = if gt then i + Int64(gt) - 1 else n + 1

        # Reverse-complement the region, dropping newlines, as a comprehension.
        rc := [comp[Int64(d) + 1]! for d in data.slice(seq_start, i - 1).reversed() if Int64(d) != 10]

        # Re-wrap the clean sequence to 60 columns into `out`, then emit it.
        record_start := p + 1
        col := Int64(0)
        for b in rc
            p += 1
            out[p] = b
            col += 1
            if col == 60
                p += 1
                out[p] = Byte(10)
                col = 0
        if col > 0
            p += 1
            out[p] = Byte(10)
        emit(out[].slice(record_start, p))!
