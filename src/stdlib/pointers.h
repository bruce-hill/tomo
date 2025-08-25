// Type infos and methods for Pointer types

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

Text_t Pointer$as_text(const void *x, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Pointer$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointer$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Pointer$is_none(const void *x, const TypeInfo_t *);
void Pointer$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Pointer$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Null(t) (t *)NULL
#define POINTER_TYPE(_sigil, _pointed)                                                                                 \
    (&(TypeInfo_t){.size = sizeof(void *),                                                                             \
                   .align = __alignof__(void *),                                                                       \
                   .tag = PointerInfo,                                                                                 \
                   .PointerInfo.sigil = _sigil,                                                                        \
                   .PointerInfo.pointed = _pointed})

#define Pointer$metamethods                                                                                            \
    {                                                                                                                  \
        .as_text = Pointer$as_text,                                                                                    \
        .compare = Pointer$compare,                                                                                    \
        .equal = Pointer$equal,                                                                                        \
        .is_none = Pointer$is_none,                                                                                    \
        .serialize = Pointer$serialize,                                                                                \
        .deserialize = Pointer$deserialize,                                                                            \
    }

#define Pointer$info(sigil_expr, pointed_info)                                                                         \
    &((TypeInfo_t){.size = sizeof(void *),                                                                             \
                   .align = __alignof__(void *),                                                                       \
                   .tag = PointerInfo,                                                                                 \
                   .PointerInfo = {.sigil = sigil_expr, .pointed = pointed_info},                                      \
                   .metamethods = Pointer$metamethods})
