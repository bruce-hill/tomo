#pragma once

// Optional types

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

#define OptionalBool_t uint8_t
#define OptionalArray_t Array_t
#define OptionalTable_t Table_t
#define OptionalText_t Text_t
#define OptionalClosure_t Closure_t

#define NONE_ARRAY ((Array_t){.length=-1})
#define NONE_BOOL ((OptionalBool_t)2)
#define NONE_INT ((OptionalInt_t){.small=0})
#define NONE_TABLE ((OptionalTable_t){.entries.length=-1})
#define NONE_CLOSURE ((OptionalClosure_t){.fn=NULL})
#define NONE_TEXT ((OptionalText_t){.length=-1})
#define NONE_MOMENT ((OptionalMoment_t){.tv_usec=-1})
#define NONE_PATH ((Path_t){.root=PATH_NONE})

PUREFUNC bool is_null(const void *obj, const TypeInfo_t *non_optional_type);
PUREFUNC uint64_t Optional$hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Optional$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Optional$equal(const void *x, const void *y, const TypeInfo_t *type);
Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo_t *type);
void Optional$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Optional$deserialize(FILE *in, void *outval, Array_t *pointers, const TypeInfo_t *type);

#define Optional$metamethods ((metamethods_t){ \
    .hash=Optional$hash, \
    .compare=Optional$compare, \
    .equal=Optional$equal, \
    .as_text=Optional$as_text, \
    .serialize=Optional$serialize, \
    .deserialize=Optional$deserialize, \
})

#define Optional$info(_size, _align, t) &((TypeInfo_t){.size=_size, .align=_align, \
                           .tag=OptionalInfo, .OptionalInfo.type=t, \
                           .metamethods=Optional$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
