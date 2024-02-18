#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"

struct TypeInfo;

typedef uint32_t (*hash_fn_t)(const void*, const struct TypeInfo*);
typedef int32_t (*compare_fn_t)(const void*, const void*, const struct TypeInfo*);
typedef bool (*equal_fn_t)(const void*, const void*, const struct TypeInfo*);
typedef CORD (*str_fn_t)(const void*, bool, const struct TypeInfo*);

typedef struct TypeInfo {
    int64_t size, align;
    struct { // Anonymous tagged union for convenience 
        enum { CustomInfo, PointerInfo, ArrayInfo, TableInfo, FunctionInfo, TypeInfoInfo, OpaqueInfo, } tag;
        union {
            struct {
                equal_fn_t equal;
                compare_fn_t compare;
                hash_fn_t hash;
                str_fn_t as_str;
            } CustomInfo;
            struct {
                const char *sigil;
                struct TypeInfo *pointed;
            } PointerInfo;
            struct {
                struct TypeInfo *item;
            } ArrayInfo;
            struct {
                struct TypeInfo *key, *value;
            } TableInfo;
            struct {
                const char *type_str;
            } FunctionInfo;
            struct {
                const char *type_str;
            } TypeInfoInfo;
            struct {} OpaqueInfo;
        };
    };
} TypeInfo;

typedef struct {
    TypeInfo type;
} TypeInfo_namespace_t;

extern TypeInfo_namespace_t TypeInfo_namespace;

CORD Type__as_str(const void *typeinfo, bool colorize, const TypeInfo *type);
CORD Func__as_str(const void *fn, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
