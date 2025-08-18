#pragma once

// Type info and methods for "Memory" opaque type

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

extern const TypeInfo_t Memoryヽinfo;
Text_t Memoryヽas_text(const void *p, bool colorize, const TypeInfo_t *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
