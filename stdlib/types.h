#pragma once

// Type information and methods for TypeInfos (i.e. runtime representations of types)

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"

typedef struct TypeInfo_s TypeInfo_t;

typedef uint64_t (*hash_fn_t)(const void*, const TypeInfo_t*);
typedef int32_t (*compare_fn_t)(const void*, const void*, const TypeInfo_t*);
typedef bool (*equal_fn_t)(const void*, const void*, const TypeInfo_t*);
typedef Text_t (*text_fn_t)(const void*, bool, const TypeInfo_t*);

typedef struct {
    const char *name;
    const TypeInfo_t *type;
} NamedType_t;

struct TypeInfo_s {
    int64_t size, align;
    struct { // Anonymous tagged union for convenience 
        enum { Invalid, CustomInfo, StructInfo, EnumInfo, PointerInfo, TextInfo, ArrayInfo, ChannelInfo, TableInfo, FunctionInfo,
            OptionalInfo, TypeInfoInfo, OpaqueInfo, CStringInfo } tag;
        union {
            struct {
                equal_fn_t equal;
                compare_fn_t compare;
                hash_fn_t hash;
                text_fn_t as_text;
            } CustomInfo;
            struct {
                const char *sigil;
                const TypeInfo_t *pointed;
            } PointerInfo;
            struct {
                const char *lang;
            } TextInfo;
            struct {
                const TypeInfo_t *item;
            } ArrayInfo, ChannelInfo;
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
#pragma GCC diagnostic ignored "-Wpedantic"
            struct {} OpaqueInfo;
            struct {
                const char *name;
                int num_tags;
                NamedType_t *tags;
            } EnumInfo;
            struct {
                const char *name;
                int num_fields;
                bool is_secret:1;
                NamedType_t *fields;
            } StructInfo;
        };
    };
};

#define Pointer$info(sigil_expr, pointed_info) &((TypeInfo_t){.size=sizeof(void*), .align=__alignof__(void*), \
                                                      .tag=PointerInfo, .PointerInfo={.sigil=sigil_expr, .pointed=pointed_info}})
#define Array$info(item_info) &((TypeInfo_t){.size=sizeof(Array_t), .align=__alignof__(Array_t), \
                                .tag=ArrayInfo, .ArrayInfo.item=item_info})
#define Set$info(item_info) &((TypeInfo_t){.size=sizeof(Table_t), .align=__alignof__(Table_t), \
                              .tag=TableInfo, .TableInfo.key=item_info, .TableInfo.value=&Void$info})
#define Channel$info(item_info) &((TypeInfo_t){.size=sizeof(Channel_t), .align=__alignof__(Channel_t), \
                                .tag=ChannelInfo, .ChannelInfo.item=item_info})
#define Table$info(key_expr, value_expr) &((TypeInfo_t){.size=sizeof(Table_t), .align=__alignof__(Table_t), \
                                           .tag=TableInfo, .TableInfo.key=key_expr, .TableInfo.value=value_expr})
#define Function$info(typestr) &((TypeInfo_t){.size=sizeof(void*), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr})
#define Closure$info(typestr) &((TypeInfo_t){.size=sizeof(void*[2]), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr})
#define Type$info(typestr) &((TypeInfo_t){.size=sizeof(TypeInfo_t), .align=__alignof__(TypeInfo_t), \
                             .tag=TypeInfoInfo, .TypeInfoInfo.type_str=typestr})
#define Optional$info(_size, _align, t) &((TypeInfo_t){.size=_size, .align=_align, \
                           .tag=OptionalInfo, .OptionalInfo.type=t})

extern const TypeInfo_t Void$info;
extern const TypeInfo_t Abort$info;
#define Void_t void

Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo_t *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
