
func main()
    say("Test bytes:")
    >> Byte(100)
    = Byte(0x64)

    >> Byte(0xFF)
    = Byte(0xFF)

    >> b := Byte(0x0F)
    >> b.hex()
    = "0F"
    >> b.hex(prefix=yes)
    = "0x0F"
    >> b.hex(uppercase=no)
    = "0f"
