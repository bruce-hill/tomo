#pragma once

// Type info and methods for "Memory" opaque type

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

extern const TypeInfo Memory;
CORD Memory__as_text(const void *p, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
