// The logic for unsigned bytes
#include <stdbool.h>
#include <stdint.h>

#include "bytes.h"
#include "integers.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

public const Byte_t Byte$min = 0;
public const Byte_t Byte$max = UINT8_MAX;

PUREFUNC public Text_t Byte$as_text(const void *b, bool colorize, const TypeInfo_t *info)
{
    (void)info;
    if (!b) return Text("Byte");
    Byte_t byte = *(Byte_t*)b;
    char digits[] = {'0', 'x', 
        (byte / 16) <= 9 ? '0' + (byte / 16) : 'a' + (byte / 16) - 10,
        (byte & 15) <= 9 ? '0' + (byte & 15) : 'a' + (byte & 15) - 10,
        '\0',
    };
    Text_t text = Text$from_str(digits);
    if (colorize) text = Texts(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

public CONSTFUNC bool Byte$is_between(const Byte_t x, const Byte_t low, const Byte_t high) {
    return low <= x && x <= high;
}

public OptionalByte_t Byte$parse(Text_t text, Text_t *remainder)
{
    OptionalInt_t full_int = Int$parse(text, remainder);
    if (full_int.small != 0L
        && Int$compare_value(full_int, I(0)) >= 0
        && Int$compare_value(full_int, I(255)) <= 0) {
        return (OptionalByte_t){.value=Byte$from_int(full_int, true)};
    } else {
        return NONE_BYTE;
    }
}

public Text_t Byte$hex(Byte_t byte, bool uppercase, bool prefix) {
    struct Text_s text = {.tag=TEXT_ASCII};
    text.ascii = GC_MALLOC_ATOMIC(8);
    char *p = (char*)text.ascii;
    if (prefix) {
        *(p++) = '0';
        *(p++) = 'x';
    }

    if (uppercase) {
        *(p++) = (byte/16) > 9 ? 'A' + (byte/16) - 10 : '0' + (byte/16);
        *(p++) = (byte & 15) > 9 ? 'A' + (byte & 15) - 10 : '0' + (byte & 15);
    } else {
        *(p++) = (byte/16) > 9 ? 'a' + (byte/16) - 10 : '0' + (byte/16);
        *(p++) = (byte & 15) > 9 ? 'a' + (byte & 15) - 10 : '0' + (byte & 15);
    }
    text.length = (int64_t)(p - text.ascii);
    return text;
}

public bool Byte$get_bit(Byte_t x, Int_t bit_index) {
    if (Int$compare_value(bit_index, I(1)) < 0)
        fail("Invalid bit index (expected 1 or higher): ", bit_index);
    if (Int$compare_value(bit_index, I(8)) > 0)
        fail("Bit index is too large! There are only 8 bits in a byte, but index is: ", bit_index);
    return ((x & (Byte_t)(1L << (Int64$from_int(bit_index, true)-1L))) != 0);
}

#ifdef __TINYC__
#define __builtin_add_overflow(x, y, result) ({ *(result) = (x) + (y); false; })
#endif

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
    if unlikely (!truncate && Int$compare_value(i, I_small(0xFF)) > 0)
        fail("This value is too large to convert to a byte without truncation: ", i);
    else if unlikely (!truncate && Int$compare_value(i, I_small(0)) < 0)
        fail("Negative values can't be converted to bytes: ", i);
    return (Byte_t)(i.small >> 2);
}
public PUREFUNC Byte_t Byte$from_int64(Int64_t i, bool truncate) {
    if unlikely (!truncate && i != (Int64_t)(Byte_t)i)
        fail("This value can't be converted to a byte without truncation: ", i);
    return (Byte_t)i;
}
public PUREFUNC Byte_t Byte$from_int32(Int32_t i, bool truncate) {
    if unlikely (!truncate && i != (Int32_t)(Byte_t)i)
        fail("This value can't be converted to a byte without truncation: ", i);
    return (Byte_t)i;
}
public PUREFUNC Byte_t Byte$from_int16(Int16_t i, bool truncate) {
    if unlikely (!truncate && i != (Int16_t)(Byte_t)i)
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
