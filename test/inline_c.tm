
func main():
    >> inline C:Int32 { int x = 1 + 2; x }
    = 3[32]

    >> inline C {
        say(Text("Inline C code works!"), true);
    }
