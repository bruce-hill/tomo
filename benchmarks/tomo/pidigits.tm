# pidigits, from The Computer Language Benchmarks Game
#
# A Tomo port of Mr Ledrug's streaming spigot (the gcc-1 reference). This is a
# pure big-integer benchmark: the reference uses GMP's mpz_t throughout, and
# Tomo's default `Int` is exactly that, a GMP-backed arbitrary-precision
# integer (small values stay tagged; big ones spill to bignum), so the port is
# a near-transliteration with no inline C.
#
# Design notes:
#   - acc/den/num/k are all `Int` (bignum). Only the digit counters (`i`, `n`)
#     and the column index are native `Int64`, since they never grow.
#   - Tomo's `//` is Euclidean integer division, but every operand here is
#     non-negative, so it matches the reference's truncating `mpz_tdiv_q`.
#   - Output is written as raw bytes through a `byte_writer` (like fasta): the
#     ten digits of a line go into a small stack buffer, then the "\t:<count>\n"
#     tail is appended, so there's no per-digit Text allocation. This matches
#     the reference's `putchar` + `printf("\t:%u\n", i)` format exactly.
#
# Usage: pidigits <n>   (n a multiple of 10, e.g. ./pidigits 10000)

func extract_digit(num:Int, acc:Int, den:Int, nth:Int -> Int)
    return (num*nth + acc) // den

func main(n:Int64)
    acc := Int(0)
    den := Int(1)
    num := Int(1)
    k := Int(0)

    emit := (/dev/stdout).byte_writer(append=yes)
    row := &[Byte(0) for _ in 10]  # ten digit bytes of the current line
    col := Int64(0)
    i := Int64(0)
    while i < n
        # next_term(++k)
        k += 1
        k2 := k*2 + 1
        acc = (acc + num*2) * k2
        den = den * k2
        num = num * k
        if num > acc
            continue
        d := extract_digit(num, acc, den, 3)
        if d != extract_digit(num, acc, den, 4)
            continue

        col += 1
        row[col] = Byte(48 + d)
        i += 1
        if col == 10
            emit(row[])!
            emit("\t:$i\n".utf8())!
            col = 0

        # eliminate_digit(d)
        acc = (acc - den*d) * 10
        num = num * 10

    # Flush any trailing partial line (empty when n is a multiple of 10) and
    # close the stream. The reference doesn't pad a partial final line.
    emit(row[].to(Int(col)), close=yes)!
