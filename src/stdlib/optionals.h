#pragma once

// Optional types

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

#define NONE_LIST ((List_t){.length=-1})
#define NONE_BOOL ((OptionalBool_t)2)
#define NONE_INT ((OptionalInt_t){.small=0})
#define NONE_TABLE ((OptionalTable_t){.entries.length=-1})
#define NONE_CLOSURE ((OptionalClosure_t){.fn=NULL})
#define NONE_TEXT ((OptionalText_t){.length=-1})
#define NONE_PATH ((Path_t){.type=PATH_NONE})

PUREFUNC bool is_none(const void *obj, const TypeInfo_t *non_optional_type);
PUREFUNC uint64_t Optionalヽhash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Optionalヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Optionalヽequal(const void *x, const void *y, const TypeInfo_t *type);
Text_t Optionalヽas_text(const void *obj, bool colorize, const TypeInfo_t *type);
void Optionalヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Optionalヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Optionalヽmetamethods { \
    .hash=Optionalヽhash, \
    .compare=Optionalヽcompare, \
    .equal=Optionalヽequal, \
    .as_text=Optionalヽas_text, \
    .serialize=Optionalヽserialize, \
    .deserialize=Optionalヽdeserialize, \
}

#define Optionalヽinfo(_size, _align, t) &((TypeInfo_t){.size=_size, .align=_align, \
                           .tag=OptionalInfo, .OptionalInfo.type=t, \
                           .metamethods=Optionalヽmetamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
