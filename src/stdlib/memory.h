// Type info and methods for "Memory" opaque type

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes/typeinfo.h"

extern const TypeInfo_t Memory$info;
Text_t Memory$as_text(const void *p, bool colorize, const TypeInfo_t *type);
