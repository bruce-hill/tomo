// Metamethods for structs

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC uint64_t Structヽhash(const void *obj, const TypeInfo_t *type);
PUREFUNC uint64_t PackedDataヽhash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Structヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Structヽequal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool PackedDataヽequal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC Text_t Structヽas_text(const void *obj, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Structヽis_none(const void *obj, const TypeInfo_t *type);
void Structヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Structヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Structヽmetamethods { \
    .hash=Structヽhash, \
    .compare=Structヽcompare, \
    .equal=Structヽequal, \
    .as_text=Structヽas_text, \
    .is_none=Structヽis_none, \
    .serialize=Structヽserialize, \
    .deserialize=Structヽdeserialize, \
}

#define PackedDataヽmetamethods { \
    .hash=PackedDataヽhash, \
    .compare=Structヽcompare, \
    .equal=PackedDataヽequal, \
    .as_text=Structヽas_text, \
    .is_none=Structヽis_none, \
    .serialize=Structヽserialize, \
    .deserialize=Structヽdeserialize, \
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
