#pragma once

// Type information and methods for TypeInfos (i.e. runtime representations of types)

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
        enum { CustomInfo, PointerInfo, TextInfo, ArrayInfo, TableInfo, FunctionInfo, TypeInfoInfo, OpaqueInfo, } tag;
        union {
            struct {
                equal_fn_t equal;
                compare_fn_t compare;
                hash_fn_t hash;
                str_fn_t as_text;
            } CustomInfo;
            struct {
                const char *sigil;
                bool is_optional;
                const struct TypeInfo *pointed;
            } PointerInfo;
            struct {
                const char *lang;
            } TextInfo;
            struct {
                const struct TypeInfo *item;
            } ArrayInfo;
            struct {
                const struct TypeInfo *key, *value;
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

#define $PointerInfo(sigil_expr, pointed_info, opt) &((TypeInfo){.size=sizeof(void*), .align=__alignof__(void*), \
                                                 .tag=PointerInfo, .PointerInfo={.sigil=sigil_expr, .pointed=pointed_info, .is_optional=opt}})
#define $ArrayInfo(item_info) &((TypeInfo){.size=sizeof(array_t), .align=__alignof__(array_t), \
                                .tag=ArrayInfo, .ArrayInfo.item=item_info})
#define $TableInfo(key_expr, value_expr) &((TypeInfo){.size=sizeof(table_t), .align=__alignof__(table_t), \
                                           .tag=TableInfo, .TableInfo.key=key_expr, .TableInfo.value=value_expr})
#define $FunctionInfo(typestr) &((TypeInfo){.size=sizeof(void*), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr})
#define $ClosureInfo(typestr) &((TypeInfo){.size=2*sizeof(void*), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr})
#define $TypeInfoInfo(typestr) &((TypeInfo){.size=sizeof(TypeInfo), .align=__alignof__(TypeInfo), \
                                 .tag=TypeInfoInfo, .TypeInfoInfo.type_str=typestr})

extern const TypeInfo $TypeInfo;
extern const TypeInfo $Void;
extern const TypeInfo $Abort;
#define Void_t void

CORD Type$as_text(const void *typeinfo, bool colorize, const TypeInfo *type);
CORD Func$as_text(const void *fn, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
