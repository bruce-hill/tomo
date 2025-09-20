
func main()
    >> C_code:Int32`int x = 1 + 2; x`
    = Int32(3)

    >> C_code `
        say(Text("Inline C code works!"), true);
    `
