#pragma once
// Metamethods are methods that all types share:

#include <stdint.h>

#include "types.h"
#include "util.h"

PUREFUNC uint64_t generic_hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t generic_compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool generic_equal(const void *x, const void *y, const TypeInfo_t *type);
Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo_t *type);
int generic_print(const void *obj, bool colorize, const TypeInfo_t *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
