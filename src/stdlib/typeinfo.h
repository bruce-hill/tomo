// Methods and constants for TypeInfos (i.e. runtime representations of types)

#pragma once

#include "datatypes/typeinfo.h" // IWYU pragma: export
// Type$info() below expands to these at the caller's site:
#include "metamethods.h" // IWYU pragma: export

extern const TypeInfo_t Void$info;
extern const TypeInfo_t Abort$info;

Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo_t *type);

#define Type$info(typestr)                                                                                             \
    &((TypeInfo_t){                                                                                                    \
        .size = sizeof(TypeInfo_t),                                                                                    \
        .align = __alignof__(TypeInfo_t),                                                                              \
        .tag = TypeInfoInfo,                                                                                           \
        .TypeInfoInfo.type_str = typestr,                                                                              \
        .metamethods = {.serialize = cannot_serialize, .deserialize = cannot_deserialize, .as_text = Type$as_text}})
