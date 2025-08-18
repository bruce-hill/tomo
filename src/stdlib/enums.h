// Metamethods for enums

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC uint64_t Enumヽhash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Enumヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Enumヽequal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC Text_t Enumヽas_text(const void *obj, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Enumヽis_none(const void *obj, const TypeInfo_t *type);
void Enumヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Enumヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Enumヽmetamethods { \
    .as_text=Enumヽas_text, \
    .compare=Enumヽcompare, \
    .equal=Enumヽequal, \
    .hash=Enumヽhash, \
    .is_none=Enumヽis_none, \
    .serialize=Enumヽserialize, \
    .deserialize=Enumヽdeserialize, \
}

#define PackedDataEnumヽmetamethods { \
    .hash=PackedDataヽhash, \
    .compare=Enumヽcompare, \
    .equal=PackedDataヽequal, \
    .as_text=Enumヽas_text, \
    .is_none=Enumヽis_none, \
    .serialize=Enumヽserialize, \
    .deserialize=Enumヽdeserialize, \
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
