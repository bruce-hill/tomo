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
#     case) to its upper-case complement, and everything else — crucially
#     newlines — to 0. Walking the sequence backwards and emitting only the
#     non-zero lookups reverses, complements, upper-cases and strips newlines in
#     a single pass, exactly like the reference's `xtab` (where `if (c)` skips
#     the zero entries).
#   - Header/record boundaries are located with `.find()` (memchr-backed,
#     vectorized) instead of a manual per-byte scan loop — the same technique
#     the fast reference implementations use (Go's line-splitting bufio
#     reader, C#'s `Array.IndexOf`), which a hand-written scalar loop can't
#     match without SIMD.
#   - The output buffer is `data`'s own bytes reused as scratch space (plus a
#     little padding): a `[Byte]` slice shares its backing storage (CoW) until
#     the first write, which triggers exactly one bulk copy-on-write compact
#     for the *whole* buffer; every write after that is a plain bounds-checked
#     pointer store, with no growth logic and no per-byte `memcpy()` call. This
#     replaced a per-byte `out.insert(c)`, which (even after fixing
#     `List$insert`'s growth path to bulk-copy) still called a real `memcpy()`
#     once per byte to place the new item.
#   - Output goes through a raw `byte_writer` (like fasta), one whole record at
#     a time.
#
# Usage: reversecomplement < fasta_input   (stdin is FASTA text)

# Build the complement lookup: index by byte value + 1 (lists are 1-indexed).
# Zeros elsewhere mean "drop this byte" (newlines, stray characters).
func build_comp(-> [Byte])
    letters : [Byte] = [65, 66, 67, 68, 71, 72, 75, 77, 78, 82, 83, 84, 86, 87, 89]
    comps : [Byte] = [84, 86, 71, 72, 67, 68, 77, 75, 78, 89, 83, 65, 66, 87, 82]
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

    # Reused write buffer for every record's output, shared across the whole
    # run. Output can never be longer than the input (re-wrapping only removes
    # or repositions newlines), so `n` bytes of `data` itself, plus a little
    # slack for a wrap-width mismatch between input and output, is always
    # enough room. Sliced (not copied) from `data` — the first indexed write
    # below triggers one bulk copy-on-write compact, not a per-byte cost.
    out := &(data ++ [Byte(0) for _ in n / 60 + 64])
    p := Int64(0)  # write cursor into `out`, shared across all records

    i := Int64(1)
    while i <= n
        # Header line: from '>' up to (not including) the newline. `.find()`
        # on a `[Byte]` is memchr-backed (vectorized), so this locates the
        # newline in one call instead of a manual per-byte scan.
        hstart := i
        nl := data.from(i).find(Byte(10))
        i = if nl then i + Int64(nl) - 1 else n + 1
        emit(data.slice(hstart, i - 1))!
        emit([Byte(10)])!
        i += 1  # step past the newline

        # Sequence bytes run until the next '>' (or EOF), found the same way.
        seq_start := i
        if i <= n
            gt := data.from(i).find(Byte(62))
            i = if gt then i + Int64(gt) - 1 else n + 1
        seq_end := i  # exclusive

        # Walk the sequence backwards, emitting only non-zero complements, and
        # re-wrap at 60 columns. Newlines map to 0 and are skipped for free.
        record_start := p + 1
        col := Int64(0)
        j := seq_end - 1
        while j >= seq_start
            c := comp[Int64(data[j]!) + 1]!
            if Int64(c) != 0
                p += 1
                out[p] = c
                col += 1
                if col == 60
                    p += 1
                    out[p] = Byte(10)
                    col = 0
            j -= 1
        if col > 0
            p += 1
            out[p] = Byte(10)
        emit(out[].slice(record_start, p))!
