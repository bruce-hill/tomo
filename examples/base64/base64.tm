# Base 64 encoding and decoding

_enc := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/":utf8_bytes()

_EQUAL_BYTE := 0x3D[B]

_dec := [
    255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 255[B], 255[B], 62[B],  255[B], 255[B], 255[B], 63[B],
    52[B],  53[B],  54[B],  55[B],  56[B],  57[B],  58[B],  59[B],
    60[B],  61[B],  255[B], 255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 0[B],   1[B],   2[B],   3[B],   4[B],   5[B],   6[B],
    7[B],   8[B],   9[B],   10[B],  11[B],  12[B],  13[B],  14[B],
    15[B],  16[B],  17[B],  18[B],  19[B],  20[B],  21[B],  22[B],
    23[B],  24[B],  25[B],  255[B], 255[B], 255[B], 255[B], 255[B],
    255[B], 26[B],  27[B],  28[B],  29[B],  30[B],  31[B],  32[B],
    33[B],  34[B],  35[B],  36[B],  37[B],  38[B],  39[B],  40[B],
    41[B],  42[B],  43[B],  44[B],  45[B],  46[B],  47[B],  48[B],
    49[B],  50[B],  51[B],  255[B], 255[B], 255[B], 255[B], 255[B],
]

lang Base64:
    func from_text(text:Text -> Base64?):
        return Base64.from_bytes(text:utf8_bytes())

    func from_bytes(bytes:[Byte] -> Base64?):
        output := [0[B] for _ in bytes.length * 4 / 3 + 4]
        src := 1[64]
        dest := 1[64]
        while src + 2[64] <= bytes.length:
            chunk24 := (
                (Int32(bytes[src]) <<< 16) or (Int32(bytes[src+1[64]]) <<< 8) or Int32(bytes[src+2[64]])
            )
            src += 3

            output[dest]       = _enc[1[32] + ((chunk24 >>> 18) and 0b111111[32])]
            output[dest+1[64]] = _enc[1[32] + ((chunk24 >>> 12) and 0b111111[32])]
            output[dest+2[64]] = _enc[1[32] + ((chunk24 >>> 6) and 0b111111[32])]
            output[dest+3[64]] = _enc[1[32] + (chunk24 and 0b111111[32])]
            dest += 4

        if src + 1[64] == bytes.length:
            chunk16 := (
                (Int32(bytes[src]) <<< 8) or Int32(bytes[src+1[64]])
            )
            output[dest]       = _enc[1[32] + ((chunk16 >>> 10) and 0b111111[32])]
            output[dest+1[64]] = _enc[1[32] + ((chunk16 >>> 4) and 0b111111[32])]
            output[dest+2[64]] = _enc[1[32] + ((chunk16 <<< 2)and 0b111111[32])]
            output[dest+3[64]] = _EQUAL_BYTE
        else if src == bytes.length:
            chunk8 := Int32(bytes[src])
            output[dest]       = _enc[1[32] + ((chunk8 >>> 2) and 0b111111[32])]
            output[dest+1[64]] = _enc[1[32] + ((chunk8 <<< 4) and 0b111111[32])]
            output[dest+2[64]] = _EQUAL_BYTE
            output[dest+3[64]] = _EQUAL_BYTE

        return Base64.without_escaping(Text.from_bytes(output) or return !Base64)

    func decode_text(b64:Base64 -> Text?):
        return Text.from_bytes(b64:decode_bytes() or return !Text)

    func decode_bytes(b64:Base64 -> [Byte]?):
        bytes := b64.text_content:utf8_bytes()
        output := [0[B] for _ in bytes.length/4 * 3]
        src := 1[64]
        dest := 1[64]
        while src + 3[64] <= bytes.length:
            chunk24 := (
                (Int32(_dec[1[B]+bytes[src]]) <<< 18) or
                (Int32(_dec[1[B]+bytes[src+1[64]]]) <<< 12) or
                (Int32(_dec[1[B]+bytes[src+2[64]]]) <<< 6) or
                Int32(_dec[1[B]+bytes[src+3[64]]])
            )
            src += 4

            output[dest]       = Byte((chunk24 >>> 16) and 0xFF[32])
            output[dest+1[64]] = Byte((chunk24 >>> 8) and 0xFF[32])
            output[dest+2[64]] = Byte(chunk24 and 0xFF[32])
            dest += 3

        while output[-1] == 0xFF[B]:
            output = output:to(-2)

        return output

func main(input=(/dev/stdin), decode=no):
    if decode:
        b := Base64.without_escaping(input:read()!)
        say(b:decode_text()!)
    else:
        text := input:read()!
        say(Base64.from_text(text)!.text_content)
