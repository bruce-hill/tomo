#pragma once

// An unsigned byte datatype

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#define Byte(b) ((Byte_t)(b))

PUREFUNC Text_t Byteヽas_text(const void *b, bool colorize, const TypeInfo_t *type);

Byte_t Byteヽfrom_int(Int_t i, bool truncate);
Byte_t Byteヽfrom_int64(int64_t i, bool truncate);
Byte_t Byteヽfrom_int32(int32_t i, bool truncate);
Byte_t Byteヽfrom_int16(int16_t i, bool truncate);
OptionalByte_t Byteヽparse(Text_t text, Text_t *remainder);
Closure_t Byteヽto(Byte_t first, Byte_t last, OptionalInt8_t step);
MACROLIKE Byte_t Byteヽfrom_int8(int8_t i) { return (Byte_t)i; }
MACROLIKE Byte_t Byteヽfrom_bool(bool b) { return (Byte_t)b; }
CONSTFUNC bool Byteヽis_between(const Byte_t x, const Byte_t low, const Byte_t high);

extern const Byte_t Byteヽmin;
extern const Byte_t Byteヽmax;

extern const TypeInfo_t Byteヽinfo;

Text_t Byteヽhex(Byte_t byte, bool uppercase, bool prefix);
bool Byteヽget_bit(Byte_t x, Int_t bit_index);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
