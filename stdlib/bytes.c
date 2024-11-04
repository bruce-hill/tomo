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
    return Text$format(colorize ? "\x1b[35m%u[B]\x1b[m" : "%u[B]", *b);
}

public const TypeInfo_t Byte$info = {
    .size=sizeof(Byte_t),
    .align=__alignof__(Byte_t),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Byte$as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
