#pragma once

// Type information and methods for TypeInfos (i.e. runtime representations of types)

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"

struct TypeInfo;

typedef uint64_t (*hash_fn_t)(const void*, const struct TypeInfo*);
typedef int32_t (*compare_fn_t)(const void*, const void*, const struct TypeInfo*);
typedef bool (*equal_fn_t)(const void*, const void*, const struct TypeInfo*);
typedef Text_t (*text_fn_t)(const void*, bool, const struct TypeInfo*);

typedef struct TypeInfo {
    int64_t size, align;
    struct { // Anonymous tagged union for convenience 
        enum { CustomInfo, PointerInfo, TextInfo, ArrayInfo, ChannelInfo, TableInfo, FunctionInfo, TypeInfoInfo, OpaqueInfo, EmptyStruct } tag;
        union {
            struct {
                equal_fn_t equal;
                compare_fn_t compare;
                hash_fn_t hash;
                text_fn_t as_text;
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
            } ArrayInfo, ChannelInfo;
            struct {
                const struct TypeInfo *key, *value;
            } TableInfo;
            struct {
                const char *type_str;
            } FunctionInfo;
            struct {
                const char *type_str;
            } TypeInfoInfo;
#pragma GCC diagnostic ignored "-Wpedantic"
            struct {} OpaqueInfo;
            struct {
                const char *name;
            } EmptyStruct;
        };
    };
} TypeInfo;

#define Pointer$info(sigil_expr, pointed_info, opt) &((TypeInfo){.size=sizeof(void*), .align=__alignof__(void*), \
                                                      .tag=PointerInfo, .PointerInfo={.sigil=sigil_expr, .pointed=pointed_info, .is_optional=opt}})
#define Array$info(item_info) &((TypeInfo){.size=sizeof(Array_t), .align=__alignof__(Array_t), \
                                .tag=ArrayInfo, .ArrayInfo.item=item_info})
#define Set$info(item_info) &((TypeInfo){.size=sizeof(Table_t), .align=__alignof__(Table_t), \
                              .tag=TableInfo, .TableInfo.key=item_info, .TableInfo.value=&Void$info})
#define Channel$info(item_info) &((TypeInfo){.size=sizeof(channel_t), .align=__alignof__(channel_t), \
                                .tag=ChannelInfo, .ChannelInfo.item=item_info})
#define Table$info(key_expr, value_expr) &((TypeInfo){.size=sizeof(Table_t), .align=__alignof__(Table_t), \
                                           .tag=TableInfo, .TableInfo.key=key_expr, .TableInfo.value=value_expr})
#define Function$info(typestr) &((TypeInfo){.size=sizeof(void*), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr})
#define Closure$info(typestr) &((TypeInfo){.size=sizeof(void*[2]), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr})
#define TypeInfo$info(typestr) &((TypeInfo){.size=sizeof(TypeInfo), .align=__alignof__(TypeInfo), \
                                 .tag=TypeInfoInfo, .TypeInfoInfo.type_str=typestr})

extern const TypeInfo TypeInfo$info;
extern const TypeInfo Void$info;
extern const TypeInfo Abort$info;
#define Void_t void

Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo *type);
Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
