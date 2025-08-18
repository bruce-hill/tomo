#pragma once

// Boolean functions/type info

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "optionals.h"
#include "util.h"

#define yes (Bool_t)true
#define no (Bool_t)false

PUREFUNC Text_t Boolヽas_text(const void *b, bool colorize, const TypeInfo_t *type);
OptionalBool_t Boolヽparse(Text_t text, Text_t *remainder);
MACROLIKE Bool_t Boolヽfrom_int(Int_t i) { return (i.small != 0); }
MACROLIKE Bool_t Boolヽfrom_int64(Int64_t i) { return (i != 0); }
MACROLIKE Bool_t Boolヽfrom_int32(Int32_t i) { return (i != 0); }
MACROLIKE Bool_t Boolヽfrom_int16(Int16_t i) { return (i != 0); }
MACROLIKE Bool_t Boolヽfrom_int8(Int8_t i) { return (i != 0); }
MACROLIKE Bool_t Boolヽfrom_byte(uint8_t b) { return (b != 0); }

extern const TypeInfo_t Boolヽinfo;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
