#pragma once

// Type infos and methods for Pointer types

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

Text_t Pointerヽas_text(const void *x, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Pointerヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointerヽequal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointerヽis_none(const void *x, const TypeInfo_t*);
void Pointerヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Pointerヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Null(t) (t*)NULL
#define POINTER_TYPE(_sigil, _pointed) (&(TypeInfo_t){\
    .size=sizeof(void*), .align=alignof(void*), .tag=PointerInfo, .PointerInfo.sigil=_sigil, .PointerInfo.pointed=_pointed})

#define Pointerヽmetamethods { \
    .as_text=Pointerヽas_text, \
    .compare=Pointerヽcompare, \
    .equal=Pointerヽequal, \
    .is_none=Pointerヽis_none, \
    .serialize=Pointerヽserialize, \
    .deserialize=Pointerヽdeserialize, \
}

#define Pointerヽinfo(sigil_expr, pointed_info) &((TypeInfo_t){.size=sizeof(void*), .align=__alignof__(void*), \
                                                 .tag=PointerInfo, .PointerInfo={.sigil=sigil_expr, .pointed=pointed_info}, \
                                                 .metamethods=Pointerヽmetamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
