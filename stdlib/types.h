#pragma once

// Type information and methods for TypeInfos (i.e. runtime representations of types)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "datatypes.h"

typedef struct TypeInfo_s TypeInfo_t;

typedef struct {
    uint64_t (*hash)(const void*, const TypeInfo_t*);
    int32_t (*compare)(const void*, const void*, const TypeInfo_t*);
    bool (*equal)(const void*, const void*, const TypeInfo_t*);
    Text_t (*as_text)(const void*, bool, const TypeInfo_t*);
    bool (*is_none)(const void*, const TypeInfo_t*);
    void (*serialize)(const void*, FILE*, Table_t*, const TypeInfo_t*);
    void (*deserialize)(FILE*, void*, Array_t*, const TypeInfo_t*);
} metamethods_t;

typedef struct {
    const char *name;
    const TypeInfo_t *type;
} NamedType_t;

struct TypeInfo_s {
    int64_t size, align;
    metamethods_t metamethods;
    struct { // Anonymous tagged union for convenience 
        enum { OpaqueInfo, StructInfo, EnumInfo, PointerInfo, TextInfo, ArrayInfo, ChannelInfo, TableInfo, FunctionInfo,
            OptionalInfo, TypeInfoInfo } tag;
        union {
            struct {} OpaqueInfo;
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

extern const TypeInfo_t Void$info;
extern const TypeInfo_t Abort$info;
#define Void_t void

Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo_t *type);

#define Type$info(typestr) &((TypeInfo_t){.size=sizeof(TypeInfo_t), .align=__alignof__(TypeInfo_t), \
                             .tag=TypeInfoInfo, .TypeInfoInfo.type_str=typestr, \
                             .metamethods={.serialize=cannot_serialize, .deserialize=cannot_deserialize, .as_text=Type$as_text}})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
