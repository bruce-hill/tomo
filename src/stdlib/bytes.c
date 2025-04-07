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

public CONSTFUNC bool Byte$is_between(const Byte_t x, const Byte_t low, const Byte_t high) {
    return low <= x && x <= high;
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

typedef struct {
    OptionalByte_t current, last;
    Int8_t step;
} ByteRange_t;

static OptionalByte_t _next_Byte(ByteRange_t *info) {
    OptionalByte_t i = info->current;
    if (!i.is_none) {
        Byte_t next; bool overflow = __builtin_add_overflow(i.value, info->step, &next);
        if (overflow || (!info->last.is_none && (info->step >= 0 ? next > info->last.value : next < info->last.value)))
            info->current = (OptionalByte_t){.is_none=true};
        else
            info->current = (OptionalByte_t){.value=next};
    }
    return i;
}

public CONSTFUNC Closure_t Byte$to(Byte_t first, Byte_t last, OptionalInt8_t step) {
    ByteRange_t *range = GC_MALLOC(sizeof(ByteRange_t));
    range->current = (OptionalByte_t){.value=first};
    range->last = (OptionalByte_t){.value=last};
    range->step = step.is_none ? (last >= first ? 1 : -1) : step.value;
    return (Closure_t){.fn=_next_Byte, .userdata=range};
}

public PUREFUNC Byte_t Byte$from_int(Int_t i, bool truncate) {
    if unlikely (truncate && Int$compare_value(i, I_small(0xFF)) > 0)
        fail("This value is too large to convert to a byte without truncation: ", i);
     else if unlikely (truncate && Int$compare_value(i, I_small(0)) < 0)
        fail("Negative values can't be converted to bytes: ", i);
    return (i.small != 0);
}
public PUREFUNC Byte_t Byte$from_int64(Int64_t i, bool truncate) {
    if unlikely (truncate && i != (Int64_t)(Byte_t)i)
        fail("This value can't be converted to a byte without truncation: ", i);
    return (Byte_t)i;
}
public PUREFUNC Byte_t Byte$from_int32(Int32_t i, bool truncate) {
    if unlikely (truncate && i != (Int32_t)(Byte_t)i)
        fail("This value can't be converted to a byte without truncation: ", i);
    return (Byte_t)i;
}
public PUREFUNC Byte_t Byte$from_int16(Int16_t i, bool truncate) {
    if unlikely (truncate && i != (Int16_t)(Byte_t)i)
        fail("This value can't be converted to a byte without truncation: ", i);
    return (Byte_t)i;
}

public const TypeInfo_t Byte$info = {
    .size=sizeof(Byte_t),
    .align=__alignof__(Byte_t),
    .metamethods={
        .as_text=Byte$as_text,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
