#pragma once

// Type infos and methods for Pointer types

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

Text_t Pointer$as_text(const void *x, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Pointer$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointer$equal(const void *x, const void *y, const TypeInfo_t *type);

#define Null(t) (t*)NULL
#define POINTER_TYPE(_sigil, _pointed) (&(TypeInfo_t){\
    .size=sizeof(void*), .align=alignof(void*), .tag=PointerInfo, .PointerInfo.sigil=_sigil, .PointerInfo.pointed=_pointed})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
