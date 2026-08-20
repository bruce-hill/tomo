# spectral-norm — The Computer Language Benchmarks Game
#
# A Tomo port. Approximates the largest singular value of the infinite matrix
# A{i,j} = 1 / ((i+j)(i+j+1)/2 + i + 1) by 10 rounds of the power method on
# AᵀA, then prints sqrt(uᵀ(AᵀA)u / uᵀu) to nine decimals.
#
# Design notes:
#   - Vectors are flat `&[Float64]` buffers reused across iterations; the two
#     matrix-vector products fill a shared scratch vector.
#   - eval_A's denominator is computed inline in the hot loops as native
#     Int64 arithmetic ((i+j)(i+j+1) is always even, so the `// 2` is exact) and
#     converted to `Float64` for the divide, avoiding a per-element call.
#   - The only inline C is the final `%.9f` formatting (Tomo has no
#     zero-padded float format), matching k-nucleotide's precedent and the
#     benchmark's fixed output format.
#
# Usage: spectralnorm <n>   (e.g. ./spectralnorm 5500)

# (A·u)_i = Σ_j u_j / ((i+j)(i+j+1)/2 + i + 1)
# The denominator is a quadratic in the column j, so within a row it is carried
# incrementally: each step adds `inc` (which itself grows by 1). Every value
# stays an exact integer far below 2^53, so the Float64 carry equals the integer
# denominator exactly — but the hot loop is just two float adds and a divide,
# with no per-element integer multiply/divide/convert. `u` is iterated by
# element, so there is no bounds check or optional unwrap either.
func mult_Av(u:&[Float64], out:&[Float64], n:Int64)
    for i in Int64(0).to(n - 1)
        s : Float64 = 0.0
        d := Float64(i * (i + 1) // 2 + i + 1)  # denominator at column 0
        inc := Float64(i + 1)                  # d(j+1) - d(j) at column 0
        for u_j in u[]
            s += u_j / d
            d += inc
            inc += 1.0
        out[i + 1] = s

# (Aᵀ·u)_i = Σ_j u_j / ((i+j)(i+j+1)/2 + j + 1)
func mult_Atv(u:&[Float64], out:&[Float64], n:Int64)
    for i in Int64(0).to(n - 1)
        s : Float64 = 0.0
        d := Float64(i * (i + 1) // 2 + 1)  # denominator at column 0
        inc := Float64(i + 2)              # d(j+1) - d(j) at column 0
        for u_j in u[]
            s += u_j / d
            d += inc
            inc += 1.0
        out[i + 1] = s

# out = AᵀA·u, via the shared scratch vector `tmp`.
func mult_AtAv(u:&[Float64], out:&[Float64], tmp:&[Float64], n:Int64)
    mult_Av(u, tmp, n)
    mult_Atv(tmp, out, n)

func main(n:Int64)
    u := &[Float64(1) for _ in n]
    v := &[Float64(0) for _ in n]
    tmp := &[Float64(0) for _ in n]

    for _ in 10
        mult_AtAv(u, v, tmp, n)
        mult_AtAv(v, u, tmp, n)

    vBv : Float64 = 0.0
    vv : Float64 = 0.0
    for i in Int64(0).to(n - 1)
        vBv += u[i + 1]! * v[i + 1]!
        vv += v[i + 1]! * v[i + 1]!

    result := (vBv / vv).sqrt()
    say(C_code:Text`({ char buf[32]; snprintf(buf, sizeof(buf), "%.9f", @result); Text$from_str(buf); })`)
