#pragma once

// An unsigned byte datatype

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

#define Byte_t uint8_t
#define Byte(b) ((Byte_t)(b))

PUREFUNC Text_t Byte$as_text(const Byte_t *b, bool colorize, const TypeInfo_t *type);

#define Byte_to_Int64(b, _) ((Int64_t)(b))
#define Byte_to_Int32(b, _) ((Int32_t)(b))
#define Byte_to_Int16(b, _) ((Int16_t)(b))
#define Byte_to_Int8(b, _) ((Int8_t)(b))

#define Int64_to_Byte(b, _) ((Byte_t)(b))
#define Int32_to_Byte(b, _) ((Byte_t)(b))
#define Int16_to_Byte(b, _) ((Byte_t)(b))
#define Int8_to_Byte(b, _) ((Byte_t)(b))

extern const Byte_t Byte$min;
extern const Byte_t Byte$max;

extern const TypeInfo_t Byte$info;

Text_t Byte$hex(Byte_t byte, bool uppercase, bool prefix);

typedef struct {
    Byte_t value;
    bool is_null:1;
} OptionalByte_t;

#define NULL_BYTE ((OptionalByte_t){.is_null=true})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
