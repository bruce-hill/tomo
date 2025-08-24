#pragma once

// An unsigned byte datatype

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

#define Byte(b) ((Byte_t)(b))

PUREFUNC Text_t Byte$as_text(const void *b, bool colorize, const TypeInfo_t *type);

Byte_t Byte$from_int(Int_t i, bool truncate);
Byte_t Byte$from_int64(int64_t i, bool truncate);
Byte_t Byte$from_int32(int32_t i, bool truncate);
Byte_t Byte$from_int16(int16_t i, bool truncate);
OptionalByte_t Byte$parse(Text_t text, Text_t *remainder);
Closure_t Byte$to(Byte_t first, Byte_t last, OptionalInt8_t step);
MACROLIKE Byte_t Byte$from_int8(int8_t i) { return (Byte_t)i; }
MACROLIKE Byte_t Byte$from_bool(bool b) { return (Byte_t)b; }
CONSTFUNC bool Byte$is_between(const Byte_t x, const Byte_t low, const Byte_t high);

extern const Byte_t Byte$min;
extern const Byte_t Byte$max;

extern const TypeInfo_t Byte$info;

Text_t Byte$hex(Byte_t byte, bool uppercase, bool prefix);
bool Byte$get_bit(Byte_t x, Int_t bit_index);
