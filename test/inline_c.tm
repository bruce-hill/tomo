test "inline C expression"
    >> C_code:Int32`int x = 1 + 2; x`
    >> Int32(3)
    assert C_code:Int32`int x = 1 + 2; x` == Int32(3)

test "inline C statement"
    >> C_code `
        say(Text("Inline C code works!"), true);
    `
