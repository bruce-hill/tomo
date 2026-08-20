# fasta — The Computer Language Benchmarks Game
#
# A Tomo port. Pure Tomo; no inline C. Output must match the C reference
# byte-for-byte, which pins down the exact pseudo-random sequence (a linear
# congruential generator shared across the TWO and THREE blocks) and the
# 60-column line wrapping.
#
# The algorithm is fixed by the benchmark rules and NOT changed here: naive LCG
# (no caching the sequence), cumulative probabilities, linear search to pick a
# nucleotide. Only the *implementation* is tuned, so the hot path stays cheap:
#   - The LCG seed is a native `Int64` local threaded through each random block
#     (returned and passed back in), so the millions of updates happen in a
#     register instead of through a heap `@[Int64]` (which would cost a bounds
#     check + optional-unwrap + copy-on-write guard on every single draw).
#   - The cumulative probabilities are paired with their output byte in a
#     `[Freq]` list, and the linear search is `for f in freqs` — element
#     iteration, so no per-step bounds-checked optional indexing.
#   - Output bytes are filled straight into a fixed byte buffer sized for a
#     whole batch of lines (with newlines) and written raw to stdout with a
#     `byte_writer` — no per-line Text allocation, cord concatenation, or
#     UTF-8 round-trip.
#
# The repeated-ALU block (ONE) still leans on Tomo's cord/rope Text: `++` is
# O(1) and slicing is O(1), so `alu ++ alu.slice(1, 60)` is a cord whose every
# 60-wide window [pos+1, pos+60] is a valid O(1) slice of one shared backing
# cord — no copying.
#
# Usage: fasta <n>   (e.g. ./fasta 2500000)

_IM := Int64(139968)
_IA := Int64(3877)
_IC := Int64(29573)
_LINE := Int64(60)
_BATCH_LINES := Int64(1024)

# A cumulative probability paired with the byte to emit if it's selected.
struct Freq{cutoff:Num, code:Byte}

# In-order cumulative sums, accumulated in Num (double) exactly as the C
# reference does, so the `r < cutoff` selection picks identical characters.
# Each cutoff is paired with its output byte so the linear search below can be
# a plain element iteration.
func make_freqs(ps:[Num], codes:[Byte] -> [Freq])
    freqs : &[Freq] = &[]
    total := 0.0
    for p at i in ps
        total += p
        freqs.insert(Freq{total, codes[i]!})
    return freqs[]

func repeat_fasta(header:Text, s:Text, n:Int64)
    # One cord wide enough that every 60-char window is a valid slice:
    ext := s ++ s.slice(1, _LINE)
    len := Int64(s.length)
    pos := Int64(0) # 0-based offset of the next character into `s`
    todo := n
    batch := header ++ "\n"
    batched := Int64(1)
    while todo > 0
        m := _LINE
        if todo < _LINE
            m = todo
        batch = batch ++ ext.slice(pos+1, pos+m) ++ "\n" # O(1) slice of the cord
        pos = (pos + m) mod len
        todo -= m
        batched += 1
        if batched >= _BATCH_LINES
            say(batch, newline=no)
            batch = ""
            batched = 0
    if batch.length > 0
        say(batch, newline=no)

# Draws `n` nucleotides by weighted random selection and prints them, 60 per
# line. Returns the advanced LCG seed so a single generator threads through
# both random blocks. `seed` stays a native local, so each draw is a register
# update, not a heap access.
func random_fasta(header:Text, freqs:[Freq], n:Int64, seed:Int64 -> Int64)
    # First cutoff greater than r wins; on the floating-point edge where r is
    # >= every cutoff, the last symbol is used (matching the reference).
    last_code := freqs[freqs.length]!.code
    cap := (_LINE + 1) * _BATCH_LINES  # a full batch: lines plus their newlines
    buf := &[Byte(0) for _ in cap]   # stack scratch buffer; never escapes
    p := Int64(0)    # write cursor into `buf` (0-based; buf is 1-indexed)
    col := Int64(0)  # characters written on the current line
    # Write raw bytes straight to stdout — no UTF-8 validation or Text/cord
    # round-trip. `append` avoids seeking a pipe; the final write closes it.
    emit := (/dev/stdout).byte_writer(append=yes)
    emit((header ++ "\n").utf8())!
    s := seed
    for _ in n
        s = (s * _IA + _IC) mod _IM
        r := Num(s) / Num(_IM)
        b := last_code
        for f in freqs # linear search over cumulative probabilities
            if r < f.cutoff
                b = f.code
                break
        p += 1
        buf[p] = b
        col += 1
        if col == _LINE
            p += 1
            buf[p] = Byte(10) # newline
            col = 0
            if p >= cap
                emit(buf[])!
                p = 0
    if col > 0 # trailing partial line
        p += 1
        buf[p] = Byte(10)
    emit(buf[].to(Int(p)), close=yes)!
    return s

func main(n:Int64)
    alu := "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTC
    ....AGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCG
    ....TGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGG
    ....AGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"

    # ASCII codes for "acgtBDHKMNRSVWY" and "acgt", paired with the cumulative
    # probabilities below.
    iub_codes : [Byte] = [97, 99, 103, 116, 66, 68, 72, 75, 77, 78, 82, 83, 86, 87, 89]
    iub_freqs := make_freqs([0.27, 0.12, 0.12, 0.27,
                             0.02, 0.02, 0.02, 0.02, 0.02, 0.02,
                             0.02, 0.02, 0.02, 0.02, 0.02], iub_codes)

    hs_codes : [Byte] = [97, 99, 103, 116]
    hs_freqs := make_freqs([0.3029549426680, 0.1979883004921,
                            0.1975473066391, 0.3015094502008], hs_codes)

    # A single generator seed, shared across the TWO and THREE blocks:
    seed := Int64(42)

    repeat_fasta(">ONE Homo sapiens alu", alu, n*2)
    seed = random_fasta(">TWO IUB ambiguity codes", iub_freqs, n*3, seed)
    seed = random_fasta(">THREE Homo sapiens frequency", hs_freqs, n*5, seed)
