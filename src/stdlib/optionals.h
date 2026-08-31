// Optional types

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC bool is_none(const void *obj, const TypeInfo_t *non_optional_type);
void set_none(void *obj, const TypeInfo_t *optional_type);
PUREFUNC uint64_t Optional$hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t Optional$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Optional$equal(const void *x, const void *y, const TypeInfo_t *type);
Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo_t *type);
void Optional$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Optional$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Optional$metamethods                                                                                           \
    {                                                                                                                  \
        .hash = Optional$hash,                                                                                         \
        .compare = Optional$compare,                                                                                   \
        .equal = Optional$equal,                                                                                       \
        .as_text = Optional$as_text,                                                                                   \
        .serialize = Optional$serialize,                                                                               \
        .deserialize = Optional$deserialize,                                                                           \
    }

#define Optional$info(_size, _align, t)                                                                                \
    &((TypeInfo_t){.size = _size,                                                                                      \
                   .align = _align,                                                                                    \
                   .tag = OptionalInfo,                                                                                \
                   .OptionalInfo.type = t,                                                                             \
                   .metamethods = Optional$metamethods})
