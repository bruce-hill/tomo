# k-nucleotide — The Computer Language Benchmarks Game
#
# A Tomo port. The only inline C is the final `%.3f` frequency formatting
# (Tomo has no zero-padded float format), which the benchmark's ground rules
# allow; all counting, encoding, sorting, and lookup is pure Tomo.
#
# Reads a FASTA file on stdin, extracts the third sequence (">THREE"), and:
#   - prints the frequency (%) of every 1- and 2-nucleotide, sorted by count
#     descending then code ascending (which is alphabetical, since A<C<G<T
#     maps to 0<1<2<3 packed big-endian);
#   - prints the count of five specific oligonucleotides.
#
# Each nucleotide is packed into a 2-bit code (A=0, C=1, G=2, T=3) and a
# length-k oligonucleotide into a 2k-bit integer key, so counting is done in
# an `{Int64:Int64}` table keyed by that integer rather than by text.
#
# Usage: knucleotide < fasta_output   (e.g. ./fasta 250000 | ./knucleotide)

struct Count{key:Int64, count:Int64}

# Sort by count descending, then by key ascending (ties broken toward the
# smaller code, i.e. alphabetically).
func by_count_then_key(a, b: Count -> Int32)
    if a.count != b.count
        return b.count <> a.count
    return a.key <> b.key

# Byte -> 2-bit code, indexed by `(byte and 7) + 1` (1-indexed). Only the
# entries for A/C/G/T are ever hit; upper/lower case share the low 3 bits.
CODE8 : [Byte] = [0, 0, 0, 1, 3, 0, 0, 2]

func nucleotide_for_code(code:Int64 -> Text)
    return "ACGT".slice(code+1, code+1)

func pct3(x:Float64 -> Text)
    return C_code:Text`({ char buf[32]; snprintf(buf, sizeof(buf), "%.3f", @x); Text$from_str(buf); })`

# Read all of stdin, then collect the codes of the ">THREE" sequence: continue to
# just past that header line, then map every non-newline byte until the next
# header (or EOF) to its 2-bit code.
func read_third_sequence(-> [Byte])
    data := (/dev/stdin).read_bytes()!
    n := Int64(data.length)
    # Find the byte just after the ">THREE" header's newline.
    i := Int64(1)
    start := Int64(0)
    while i <= n
        if Int64(data[i]!) == 62 # '>'
            # A header line; check whether it is ">THREE".
            if i+5 <= n and Int64(data[i+1]!) == 84 and Int64(data[i+2]!) == 72 and Int64(data[i+3]!) == 82 and Int64(data[i+4]!) == 69 and Int64(data[i+5]!) == 69
                # Advance to the end of this header line.
                j := i
                while j <= n and Int64(data[j]!) != 10
                    j += 1
                start = j + 1
                break
        i += 1

    if start == 0
        return []

    # THREE is the last record, so its sequence runs to EOF: take every
    # non-newline byte from `start` on and map it to its 2-bit code. As a
    # single-list-source comprehension this pre-sizes and fills with inlined
    # appends (no grow-from-empty reallocation).
    return [CODE8[(Int64(b) and 7) + 1]! for b in data.from(start) if Int64(b) != 10]

# Count every length-k oligonucleotide and print each with its frequency.
func write_frequencies(seq:[Byte], k:Int64)
    n := Int64(seq.length)
    mask := (Int64(1) << (2*k)) - 1
    counts : &{Int64:Int64; default=Int64(0)} = &{}
    key := Int64(0)
    # Prime the rolling key with the first k-1 codes (step=1 keeps the range
    # ascending, so k=1 yields an empty range instead of descending).
    for i in Int64(1).to(k-1, step=1)
        key = ((key << 2) and mask) or Int64(seq[i]!)
    for i in k.to(n, step=1)
        key = ((key << 2) and mask) or Int64(seq[i]!)
        counts[key] += 1

    entries := &[Count{kk, vv} for kk, vv in counts.entries()]
    entries.sort(
        # Sort by count descending, then by key ascending (ties broken toward the
        # smaller code, i.e. alphabetically).
        func(a, b: Count -> Int32)
            if a.count != b.count
                return b.count <> a.count
            return a.key <> b.key
    )

    total := Float64(n - k + 1)
    out := ""
    for e in entries
        # Decode the key back to its nucleotide string (high 2 bits first).
        name := ""
        shift := 2*(k-1)
        while shift >= 0
            name = name ++ nucleotide_for_code((e.key >> shift) and 3)
            shift -= 2
        out = out ++ name ++ " " ++ pct3(100.0 * Float64(e.count) / total) ++ "\n"
    say(out, newline=no)

# Count occurrences of one specific oligonucleotide by sliding its exact key
# over the sequence (no table needed — only one key matters).
func write_count(seq:[Byte], oligo:Text)
    n := Int64(seq.length)
    bytes := oligo.utf8()
    k := Int64(bytes.length)
    target := Int64(0)
    for b in bytes
        target = (target << 2) or Int64(CODE8[(Int64(b) and 7) + 1]!)
    mask := (Int64(1) << (2*k)) - 1

    key := Int64(0)
    count := Int64(0)
    for i in Int64(1).to(k-1, step=1)
        key = ((key << 2) and mask) or Int64(seq[i]!)
    for i in k.to(n, step=1)
        key = ((key << 2) and mask) or Int64(seq[i]!)
        if key == target
            count += 1
    say("$count\t$oligo")

func main()
    seq := read_third_sequence()
    write_frequencies(seq, 1)
    say("")
    write_frequencies(seq, 2)
    say("")
    write_count(seq, "GGT")
    write_count(seq, "GGTA")
    write_count(seq, "GGTATT")
    write_count(seq, "GGTATTTTAATT")
    write_count(seq, "GGTATTTTAATTTATAGT")
