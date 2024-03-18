#pragma once

// Type infos and methods for Pointer types

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

CORD Pointer__as_text(const void *x, bool colorize, const TypeInfo *type);
int32_t Pointer__compare(const void *x, const void *y, const TypeInfo *type);
bool Pointer__equal(const void *x, const void *y, const TypeInfo *type);
uint32_t Pointer__hash(const void *x, const TypeInfo *type);

#define $Null(t) (t*)NULL
#define POINTER_TYPE(_sigil, _pointed) (&(TypeInfo){\
    .size=sizeof(void*), .align=alignof(void*), .tag=PointerInfo, .PointerInfo.sigil=_sigil, .PointerInfo.pointed=_pointed})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
