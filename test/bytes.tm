
func main()
    say("Test bytes:")
    assert Byte(100) == Byte(0x64)

    assert Byte(0xFF) == Byte(0xFF)

    >> b := Byte(0x0F)
    assert b.hex() == "0F"
    assert b.hex(prefix=yes) == "0x0F"
    assert b.hex(uppercase=no) == "0f"

    assert Byte(0x06).get_bit(1) == no
    assert Byte(0x06).get_bit(2) == yes
    assert Byte(0x06).get_bit(3) == yes
    assert Byte(0x06).get_bit(4) == no
