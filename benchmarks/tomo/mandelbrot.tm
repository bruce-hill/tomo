# mandelbrot — The Computer Language Benchmarks Game
#
# A Tomo port. Renders the Mandelbrot set as a PBM bitmap (P4): one bit per
# pixel, 1 = in the set, packed 8 pixels per byte MSB-first, rows padded to a
# byte boundary. Output must match the C reference byte-for-byte.
#
# The escape loop is the classic form — squares computed once and reused in the
# loop condition, membership decided by the final `zr² + zi² <= 4` — so the
# boundary at exactly 4.0 matches the reference (which continues while `<= 4`,
# i.e. escapes only when strictly greater).
#
# Design notes:
#   - All arithmetic is native `Num` (double) and the packing uses `Int64`, so
#     the hot per-pixel loop stays in registers.
#   - A whole row of packed bytes is filled into one reusable stack buffer and
#     written raw through a `byte_writer` — no per-pixel or per-line Text.
#
# Usage: mandelbrot <size>   (e.g. ./mandelbrot 16000)

func main(n:Int64)
    size := n
    inv := 2.0 / Num(size)  # maps a 0..size pixel index into the -2..0 range
    emit := (/dev/stdout).byte_writer(append=yes)
    emit("P4\n$size $size\n".utf8())!

    row_bytes := (size + 7) / 8
    buf := &[Byte(0) for _ in row_bytes]  # one packed row; reused each y
    for y in Int64(0).to(size - 1)
        ci := inv * Num(y) - 1.0
        p := Int64(0)         # write cursor into buf (0-based; buf is 1-indexed)
        bit_num := Int64(7)   # next bit to set within the current byte (MSB=7)
        byte_acc := Int64(0)
        for x in Int64(0).to(size - 1)
            cr := inv * Num(x) - 1.5
            zr := 0.0
            zi := 0.0
            zr2 := 0.0
            zi2 := 0.0
            i := Int64(0)
            while i < 50 and zr2 + zi2 <= 4.0
                zi = 2.0 * zr * zi + ci
                zr = zr2 - zi2 + cr
                zr2 = zr * zr
                zi2 = zi * zi
                i += 1
            if zr2 + zi2 <= 4.0  # never escaped -> in the set
                byte_acc = byte_acc or (Int64(1) << bit_num)
            if bit_num == 0
                p += 1
                buf[p] = Byte(byte_acc)
                bit_num = 7
                byte_acc = 0
            else
                bit_num -= 1
        if bit_num != 7  # flush a trailing partial byte (size not a multiple of 8)
            p += 1
            buf[p] = Byte(byte_acc)
        emit(buf[].to(Int(p)))!
