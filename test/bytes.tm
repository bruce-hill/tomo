test "byte literals"
    >> Byte(100)
    >> Byte(0x64)
    assert Byte(100) == Byte(0x64)
    >> Byte(0xFF)
    assert Byte(0xFF) == Byte(0xFF)

test "byte hex formatting"
    >> b := Byte(0x0F)
    >> b.hex()
    assert b.hex() == "0F"
    >> b.hex(prefix=yes)
    assert b.hex(prefix=yes) == "0x0F"
    >> b.hex(uppercase=no)
    assert b.hex(uppercase=no) == "0f"

test "byte get_bit"
    >> Byte(0x06)
    >> Byte(0x06).get_bit(1)
    assert not Byte(0x06).get_bit(1)
    >> Byte(0x06).get_bit(2)
    assert Byte(0x06).get_bit(2)
    >> Byte(0x06).get_bit(3)
    assert Byte(0x06).get_bit(3)
    >> Byte(0x06).get_bit(4)
    assert not Byte(0x06).get_bit(4)
