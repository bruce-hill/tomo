// Representation of TypeInfo_t, the runtime description of a type
//
// Every Tomo type has a `T$info` that is one of these. The struct is here,
// beside the datatype layouts, rather than with the functions in typeinfo.h,
// so that a header naming TypeInfo_t in a signature -- which is nearly all of
// them -- need not drag in the type's own API.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// A TypeInfo_t's metamethods are written in terms of these three: every type
// renders as a Text_t, serializes through a Table_t of back-references, and
// deserializes through a List_t of them. They are part of this interface
// rather than an accident of layering, so this header re-exports them and a
// consumer of TypeInfo_t need not name them itself.
#include "list.h" // IWYU pragma: export
#include "table.h" // IWYU pragma: export
#include "text.h" // IWYU pragma: export

typedef struct TypeInfo_s TypeInfo_t;

typedef void (*serialize_fn_t)(const void *, FILE *, Table_t *, const TypeInfo_t *);
typedef void (*deserialize_fn_t)(FILE *, void *, List_t *, const TypeInfo_t *);
typedef bool (*is_none_fn_t)(const void *, const TypeInfo_t *);
typedef void (*set_none_fn_t)(void *, const TypeInfo_t *);
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
    set_none_fn_t set_none;
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
