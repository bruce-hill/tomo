// The logic for unsigned bytes
#include <stdbool.h>
#include <stdint.h>

#include "bytes.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

public const Byte_t Byte$min = 0;
public const Byte_t Byte$max = UINT8_MAX;

PUREFUNC public Text_t Byte$as_text(const void *b, bool colorize, const TypeInfo_t*)
{
    if (!b) return Text("Byte");
    return Text$format(colorize ? "\x1b[35m0x%02X\x1b[m" : "0x%02X", *(Byte_t*)b);
}

public Text_t Byte$hex(Byte_t byte, bool uppercase, bool prefix) {
    struct Text_s text = {.tag=TEXT_ASCII};
    text.ascii = GC_MALLOC_ATOMIC(8);
    if (prefix && uppercase)
        text.length = (int64_t)snprintf((char*)text.ascii, 8, "0x%02X", byte);
    else if (prefix && !uppercase)
        text.length = (int64_t)snprintf((char*)text.ascii, 8, "0x%02x", byte);
    else if (!prefix && uppercase)
        text.length = (int64_t)snprintf((char*)text.ascii, 8, "%02X", byte);
    else if (!prefix && !uppercase)
        text.length = (int64_t)snprintf((char*)text.ascii, 8, "%02x", byte);
    return text;
}

public const TypeInfo_t Byte$info = {
    .size=sizeof(Byte_t),
    .align=__alignof__(Byte_t),
    .metamethods={
        .as_text=Byte$as_text,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
