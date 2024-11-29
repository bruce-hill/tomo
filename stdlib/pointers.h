#pragma once

// Type infos and methods for Pointer types

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

Text_t Pointer$as_text(const void *x, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Pointer$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointer$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointer$is_none(const void *x, const TypeInfo_t*);

#define Null(t) (t*)NULL
#define POINTER_TYPE(_sigil, _pointed) (&(TypeInfo_t){\
    .size=sizeof(void*), .align=alignof(void*), .tag=PointerInfo, .PointerInfo.sigil=_sigil, .PointerInfo.pointed=_pointed})

#define Pointer$metamethods ((metamethods_t){ \
    .as_text=Pointer$as_text, \
    .compare=Pointer$compare, \
    .equal=Pointer$equal, \
    .is_none=Pointer$is_none, \
})

#define Pointer$info(sigil_expr, pointed_info) &((TypeInfo_t){.size=sizeof(void*), .align=__alignof__(void*), \
                                                 .tag=PointerInfo, .PointerInfo={.sigil=sigil_expr, .pointed=pointed_info}, \
                                                 .metamethods=Pointer$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
