// Metamethods for structs

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC uint64_t Struct$hash(const void *obj, const TypeInfo_t *type);
PUREFUNC uint64_t PackedData$hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Struct$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Struct$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool PackedData$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC Text_t Struct$as_text(const void *obj, bool colorize, const TypeInfo_t *type);
void Struct$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Struct$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Struct$metamethods                                                                                             \
    {                                                                                                                  \
        .hash = Struct$hash,                                                                                           \
        .compare = Struct$compare,                                                                                     \
        .equal = Struct$equal,                                                                                         \
        .as_text = Struct$as_text,                                                                                     \
        .serialize = Struct$serialize,                                                                                 \
        .deserialize = Struct$deserialize,                                                                             \
    }

#define PackedData$metamethods                                                                                         \
    {                                                                                                                  \
        .hash = PackedData$hash,                                                                                       \
        .compare = Struct$compare,                                                                                     \
        .equal = PackedData$equal,                                                                                     \
        .as_text = Struct$as_text,                                                                                     \
        .serialize = Struct$serialize,                                                                                 \
        .deserialize = Struct$deserialize,                                                                             \
    }
