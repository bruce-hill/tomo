
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

    >> Byte(0x06).get_bit(1)
    = no
    >> Byte(0x06).get_bit(2)
    = yes
    >> Byte(0x06).get_bit(3)
    = yes
    >> Byte(0x06).get_bit(4)
    = no
