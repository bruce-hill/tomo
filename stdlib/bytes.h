#pragma once

// An unsigned byte datatype

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

#define Byte_t uint8_t
#define Byte(b) ((Byte_t)(b))

PUREFUNC Text_t Byte$as_text(const Byte_t *b, bool colorize, const TypeInfo_t *type);

extern const Byte_t Byte$min;
extern const Byte_t Byte$max;

extern const TypeInfo_t Byte$info;

typedef struct {
    Byte_t value;
    bool is_null:1;
} OptionalByte_t;

#define NULL_BYTE ((OptionalByte_t){.is_null=true})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
