// Representation of the Byte type

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t Byte_t;

typedef struct {
    Byte_t value;
    bool has_value : 1;
} OptionalByte_t;

#define NONE_BYTE ((OptionalByte_t){.has_value = false})
