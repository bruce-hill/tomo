# A top-level `C_code` block is emitted at file scope, so it can declare things
# that the file's functions go on to use:
C_code`
    static int tripled(int x) { return x * 3; }
`

func triple(x:Int32 -> Int32)
    return C_code:Int32`tripled(@x)`

test "inline C expression"
    >> C_code:Int32`int x = 1 + 2; x`
    >> Int32(3)
    assert C_code:Int32`int x = 1 + 2; x` == Int32(3)

test "inline C statement"
    >> C_code `
        say(Text("Inline C code works!"), true);
    `

test "top-level inline C"
    >> triple(Int32(4))
    assert triple(Int32(4)) == Int32(12)
