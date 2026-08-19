# fasta — The Computer Language Benchmarks Game
#
# A Tomo port. Pure Tomo; no inline C. Output must match the C reference
# byte-for-byte, which pins down the exact pseudo-random sequence (a linear
# congruential generator shared across the TWO and THREE blocks) and the
# 60-column line wrapping.
#
# Tomo's Text is a cord/rope: `++` is typically O(1) and slicing is O(1). The
# port leans on that where it fits, and deliberately avoids it where it
# doesn't:
#   - The repeated-ALU block (ONE) is produced entirely by *slicing*, never
#     copying: `alu ++ alu.slice(1, 60)` is a cord whose every 60-wide window
#     [pos+1, pos+60] is in range, so each line is one O(1) slice of a shared
#     backing cord.
#   - The random blocks (TWO, THREE) do NOT concatenate character by
#     character. A cord append is O(1) but allocates a node, so 20M
#     single-grapheme appends is ~8s of pure allocation — cords are for
#     joining large pieces, not building text one letter at a time. Instead
#     each 60-char line is filled into a reused byte buffer and turned into
#     Text once (`Text.from_utf8`), and those line-Texts are the pieces the
#     cord actually concatenates. ~20x faster.
#   - Lines are batched into a cord and flushed every BATCH_LINES lines
#     (`say` flushes stdout per call), keeping memory bounded while amortizing
#     the flush.
#
# Usage: fasta <n>   (e.g. ./fasta 2500000)

IM := Int64(139968)
IA := Int64(3877)
IC := Int64(29573)
LINE := Int64(60)
BATCH_LINES := Int64(1024)

# The LCG matches the reference exactly: r = last/IM with the shared `last`
# advanced in place, so a single generator threads through both random blocks.
func gen_random(state:@[Int64] -> Num)
    last := (state[1]! * IA + IC) mod IM
    state[1] = last
    return Num(last) / Num(IM)

# In-order cumulative sums, accumulated in Num (double) exactly as the C
# reference does, so the `r < cumulative[i]` selection picks identical
# characters.
func cumulative(ps:[Num] -> [Num])
    cum := @[Num(0.0) for _ in ps]
    total := 0.0
    for p at i in ps
        total += p
        cum[i] = total
    return cum[]

func repeat_fasta(header:Text, s:Text, n:Int64)
    # One cord wide enough that every 60-char window is a valid slice:
    ext := s ++ s.slice(1, LINE)
    len := Int64(s.length)
    pos := Int64(0) # 0-based offset of the next character into `s`
    todo := n
    batch := header ++ "\n"
    batched := Int64(1)
    while todo > 0
        m := LINE
        if todo < LINE
            m = todo
        batch = batch ++ ext.slice(pos+1, pos+m) ++ "\n" # O(1) slice of the cord
        pos = (pos + m) mod len
        todo -= m
        batched += 1
        if batched >= BATCH_LINES
            say(batch, newline=no)
            batch = ""
            batched = 0
    if batch.length > 0
        say(batch, newline=no)

# `codes` are the ASCII byte values of the alphabet, in the same order as
# `cum`, so a selected index maps straight to the byte written into the line
# buffer.
func random_fasta(header:Text, codes:[Byte], cum:[Num], n:Int64, state:@[Int64])
    count := Int64(codes.length)
    buf := @[Byte(0) for _ in LINE] # reused line buffer
    filled := Int64(0)
    batch := header ++ "\n"
    batched := Int64(1)
    # `step=1` keeps the range ascending, so n=0 yields an empty range rather
    # than `.to()`'s default bidirectional descent.
    for _ in Int64(1).to(n, step=1)
        r := gen_random(state)
        # First index whose cumulative probability exceeds r (last on the
        # floating-point edge where r >= every cumulative value).
        pick := count
        for i in Int64(1).to(count)
            if r < cum[i]!
                pick = i
                stop
        filled += 1
        buf[filled] = codes[pick]!
        if filled == LINE
            batch = batch ++ Text.from_utf8(buf[])! ++ "\n"
            filled = 0
            batched += 1
            if batched >= BATCH_LINES
                say(batch, newline=no)
                batch = ""
                batched = 0
    if filled > 0
        batch = batch ++ Text.from_utf8(buf[].to(Int(filled)))! ++ "\n"
    if batch.length > 0
        say(batch, newline=no)

func main(n:Int64)
    alu := "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"

    # ASCII codes for "acgtBDHKMNRSVWY" and "acgt", paired with the cumulative
    # probabilities below.
    iub_codes : [Byte] = [97, 99, 103, 116, 66, 68, 72, 75, 77, 78, 82, 83, 86, 87, 89]
    iub_cum := cumulative([0.27, 0.12, 0.12, 0.27,
                           0.02, 0.02, 0.02, 0.02, 0.02, 0.02,
                           0.02, 0.02, 0.02, 0.02, 0.02])

    hs_codes : [Byte] = [97, 99, 103, 116]
    hs_cum := cumulative([0.3029549426680, 0.1979883004921,
                          0.1975473066391, 0.3015094502008])

    # A single generator, shared across the TWO and THREE blocks:
    state := @[Int64(42)]

    repeat_fasta(">ONE Homo sapiens alu", alu, n*2)
    random_fasta(">TWO IUB ambiguity codes", iub_codes, iub_cum, n*3, state)
    random_fasta(">THREE Homo sapiens frequency", hs_codes, hs_cum, n*5, state)
