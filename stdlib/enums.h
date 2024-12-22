// Metamethods for enums

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC uint64_t Enum$hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Enum$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Enum$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC Text_t Enum$as_text(const void *obj, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Enum$is_none(const void *obj, const TypeInfo_t *type);
void Enum$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Enum$deserialize(FILE *in, void *outval, Array_t *pointers, const TypeInfo_t *type);

#define Enum$metamethods ((metamethods_t){ \
    .as_text=Enum$as_text, \
    .compare=Enum$compare, \
    .equal=Enum$equal, \
    .hash=Enum$hash, \
    .is_none=Enum$is_none, \
    .serialize=Enum$serialize, \
    .deserialize=Enum$deserialize, \
})

#define PackedDataEnum$metamethods ((metamethods_t){ \
    .hash=PackedData$hash, \
    .compare=Enum$compare, \
    .equal=PackedData$equal, \
    .as_text=Enum$as_text, \
    .is_none=Enum$is_none, \
    .serialize=Enum$serialize, \
    .deserialize=Enum$deserialize, \
})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
