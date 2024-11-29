// Metamethods for structs

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC uint64_t Struct$hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Struct$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Struct$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC Text_t Struct$as_text(const void *obj, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Struct$is_none(const void *obj, const TypeInfo_t *type);

#define Struct$metamethods ((metamethods_t){ \
    .hash=Struct$hash, \
    .compare=Struct$compare, \
    .equal=Struct$equal, \
    .as_text=Struct$as_text, \
    .is_none=Struct$is_none, \
})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
