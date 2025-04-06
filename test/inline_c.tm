
func main()
    >> inline C:Int32 { int x = 1 + 2; x }
    = Int32(3)

    >> inline C {
        say(Text("Inline C code works!"), true);
    }
