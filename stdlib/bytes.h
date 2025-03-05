#pragma once

// An unsigned byte datatype

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#define Byte(b) ((Byte_t)(b))

PUREFUNC Text_t Byte$as_text(const void *b, bool colorize, const TypeInfo_t *type);

Byte_t Byte$from_int(Int_t i, bool truncate);
Byte_t Byte$from_int64(int64_t i, bool truncate);
Byte_t Byte$from_int32(int32_t i, bool truncate);
Byte_t Byte$from_int16(int16_t i, bool truncate);
MACROLIKE Byte_t Byte$from_int8(int8_t i) { return (Byte_t)i; }
MACROLIKE Byte_t Byte$from_bool(bool b) { return (Byte_t)b; }

extern const Byte_t Byte$min;
extern const Byte_t Byte$max;

extern const TypeInfo_t Byte$info;

Text_t Byte$hex(Byte_t byte, bool uppercase, bool prefix);

typedef struct {
    Byte_t value;
    bool has_value:1;
} OptionalByte_t;

#define NONE_BYTE ((OptionalByte_t){.has_value=false})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
