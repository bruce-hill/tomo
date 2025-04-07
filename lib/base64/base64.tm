# Base 64 encoding and decoding

_enc := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".bytes()

_EQUAL_BYTE := Byte(0x3D)

_dec : [Byte] = [
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 62,  255, 255, 255, 63,
    52,  53,  54,  55,  56,  57,  58,  59,
    60,  61,  255, 255, 255, 255, 255, 255,
    255, 0,   1,   2,   3,   4,   5,   6,
    7,   8,   9,   10,  11,  12,  13,  14,
    15,  16,  17,  18,  19,  20,  21,  22,
    23,  24,  25,  255, 255, 255, 255, 255,
    255, 26,  27,  28,  29,  30,  31,  32,
    33,  34,  35,  36,  37,  38,  39,  40,
    41,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  255, 255, 255, 255, 255,
]

lang Base64
    func parse(text:Text -> Base64?)
        return Base64.from_bytes(text.bytes())

    func from_bytes(bytes:[Byte] -> Base64?)
        output := &[Byte(0) for _ in bytes.length * 4 / 3 + 4]
        src := Int64(1)
        dest := Int64(1)
        while src + 2 <= Int64(bytes.length)
            chunk24 := (
                (Int32(bytes[src]) <<< 16) or (Int32(bytes[src+1]) <<< 8) or Int32(bytes[src+2])
            )
            src += 3

            output[dest]   = _enc[1 + ((chunk24 >>> 18) and 0b111111)]
            output[dest+1] = _enc[1 + ((chunk24 >>> 12) and 0b111111)]
            output[dest+2] = _enc[1 + ((chunk24 >>> 6) and 0b111111)]
            output[dest+3] = _enc[1 + (chunk24 and 0b111111)]
            dest += 4

        if src + 1 == bytes.length
            chunk16 := (
                (Int32(bytes[src]) <<< 8) or Int32(bytes[src+1])
            )
            output[dest]   = _enc[1 + ((chunk16 >>> 10) and 0b11111)]
            output[dest+1] = _enc[1 + ((chunk16 >>> 4) and 0b111111)]
            output[dest+2] = _enc[1 + ((chunk16 <<< 2)and 0b111111)]
            output[dest+3] = _EQUAL_BYTE
        else if src == bytes.length
            chunk8 := Int32(bytes[src])
            output[dest]   = _enc[1 + ((chunk8 >>> 2) and 0b111111)]
            output[dest+1] = _enc[1 + ((chunk8 <<< 4) and 0b111111)]
            output[dest+2] = _EQUAL_BYTE
            output[dest+3] = _EQUAL_BYTE

        return Base64.from_text(Text.from_bytes(output[]) or return none)

    func decode_text(b64:Base64 -> Text?)
        return Text.from_bytes(b64.decode_bytes() or return none)

    func decode_bytes(b64:Base64 -> [Byte]?)
        bytes := b64.text.bytes()
        output := &[Byte(0) for _ in bytes.length/4 * 3]
        src := Int64(1)
        dest := Int64(1)
        while src + 3 <= Int64(bytes.length)
            chunk24 := (
                (Int32(_dec[1+bytes[src]]) <<< 18) or
                (Int32(_dec[1+bytes[src+1]]) <<< 12) or
                (Int32(_dec[1+bytes[src+2]]) <<< 6) or
                Int32(_dec[1+bytes[src+3]])
            )
            src += 4

            output[dest]   = Byte((chunk24 >>> 16) and 0xFF)
            output[dest+1] = Byte((chunk24 >>> 8) and 0xFF)
            output[dest+2] = Byte(chunk24 and 0xFF)
            dest += 3

        while output[-1] == 0xFF
            output[] = output.to(-2)

        return output[]

func main(input=(/dev/stdin), decode=no)
    if decode
        b := Base64.from_text(input.read()!)
        say(b.decode_text()!)
    else
        text := input.read()!
        say(Base64.parse(text)!.text)
