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
        enum { CustomInfo, PointerInfo, ArrayInfo, TableInfo, TypeInfoInfo, } tag;
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
                int64_t entry_size, value_offset;
            } TableInfo;
            struct {
                const char *type_str;
            } TypeInfoInfo;
        };
    };
} TypeInfo;

CORD Type__as_str(const void *typeinfo, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
