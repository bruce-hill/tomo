#pragma once

// Type information and methods for TypeInfos (i.e. runtime representations of types)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "datatypes.h"

typedef struct TypeInfo_s TypeInfo_t;

typedef void (*serialize_fn_t)(const void *, FILE *, Table_t *, const TypeInfo_t *);
typedef void (*deserialize_fn_t)(FILE *, void *, List_t *, const TypeInfo_t *);
typedef bool (*is_none_fn_t)(const void *, const TypeInfo_t *);
typedef uint64_t (*hash_fn_t)(const void *, const TypeInfo_t *);
typedef int32_t (*compare_fn_t)(const void *, const void *, const TypeInfo_t *);
typedef bool (*equal_fn_t)(const void *, const void *, const TypeInfo_t *);
typedef Text_t (*as_text_fn_t)(const void *, bool, const TypeInfo_t *);

typedef struct {
    hash_fn_t hash;
    compare_fn_t compare;
    equal_fn_t equal;
    as_text_fn_t as_text;
    is_none_fn_t is_none;
    serialize_fn_t serialize;
    deserialize_fn_t deserialize;
} metamethods_t;

typedef struct {
    const char *name;
    const TypeInfo_t *type;
} NamedType_t;

struct TypeInfo_s {
    int64_t size, align;
    metamethods_t metamethods;
    struct { // Anonymous tagged union for convenience
        enum {
            OpaqueInfo,
            StructInfo,
            EnumInfo,
            PointerInfo,
            TextInfo,
            ListInfo,
            TableInfo,
            FunctionInfo,
            OptionalInfo,
            TypeInfoInfo
        } tag;
        union {
            struct {
            } OpaqueInfo;
            struct {
                const char *sigil;
                const TypeInfo_t *pointed;
            } PointerInfo;
            struct {
                const char *lang;
            } TextInfo;
            struct {
                const TypeInfo_t *item;
            } ListInfo;
            struct {
                const TypeInfo_t *key, *value;
            } TableInfo;
            struct {
                const char *type_str;
            } FunctionInfo;
            struct {
                const char *type_str;
            } TypeInfoInfo;
            struct {
                const TypeInfo_t *type;
            } OptionalInfo;
            struct {
                const char *name;
                NamedType_t *tags;
                int num_tags;
            } EnumInfo;
            struct {
                const char *name;
                NamedType_t *fields;
                int num_fields;
                bool is_secret : 1, is_opaque : 1;
            } StructInfo;
        };
    };
};

extern const TypeInfo_t Void$info;
extern const TypeInfo_t Abort$info;
#define Void_t void

Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo_t *type);

#define Type$info(typestr)                                                                                             \
    &((TypeInfo_t){                                                                                                    \
        .size = sizeof(TypeInfo_t),                                                                                    \
        .align = __alignof__(TypeInfo_t),                                                                              \
        .tag = TypeInfoInfo,                                                                                           \
        .TypeInfoInfo.type_str = typestr,                                                                              \
        .metamethods = {.serialize = cannot_serialize, .deserialize = cannot_deserialize, .as_text = Type$as_text}})

#define DEFINE_OPTIONAL_TYPE(t, unpadded_size, name)                                                                   \
    typedef struct {                                                                                                   \
        union {                                                                                                        \
            t value;                                                                                                   \
            struct {                                                                                                   \
                char _padding[unpadded_size];                                                                          \
                Bool_t is_none;                                                                                        \
            };                                                                                                         \
        };                                                                                                             \
    } name
