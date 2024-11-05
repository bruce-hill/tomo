// The logic for unsigned bytes
#include <stdbool.h>
#include <stdint.h>

#include "bytes.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

public const Byte_t Byte$min = 0;
public const Byte_t Byte$max = UINT8_MAX;

PUREFUNC public Text_t Byte$as_text(const Byte_t *b, bool colorize, const TypeInfo_t *type)
{
    (void)type;
    if (!b) return Text("Byte");
    return Text$format(colorize ? "\x1b[36mByte\x1b[m(\x1b[35m0x%02X\x1b[m)" : "Byte(0x%02X)", *b);
}

public Text_t Byte$hex(Byte_t byte, bool uppercase, bool prefix) {
    Text_t text = {.tag=TEXT_SHORT_ASCII};
    if (prefix && uppercase)
        text.length = (int64_t)snprintf(text.short_ascii, sizeof(text.short_ascii), "0x%02X", byte);
    else if (prefix && !uppercase)
        text.length = (int64_t)snprintf(text.short_ascii, sizeof(text.short_ascii), "0x%02x", byte);
    else if (!prefix && uppercase)
        text.length = (int64_t)snprintf(text.short_ascii, sizeof(text.short_ascii), "%02X", byte);
    else if (!prefix && !uppercase)
        text.length = (int64_t)snprintf(text.short_ascii, sizeof(text.short_ascii), "%02x", byte);
    return text;
}

public const TypeInfo_t Byte$info = {
    .size=sizeof(Byte_t),
    .align=__alignof__(Byte_t),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Byte$as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
