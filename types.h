#pragma once

// Logic for defining and working with types

#include <printf.h>
#include <stdlib.h>

#include "ast.h"
#include "stdlib/lists.h"

typedef struct type_s type_t;

typedef struct arg_s {
    const char *name;
    type_t *type;
    ast_t *default_val;
    struct arg_s *next;
} arg_t;

#define ARG_LIST(...) ({\
    arg_t *args[] = {__VA_ARGS__}; \
    for (size_t i = 0; i < sizeof(args)/sizeof(args[0])-1; i++) \
        args[i]->next = args[i+1]; \
    args[0]; })

typedef struct tag_s {
    const char *name;
    int64_t tag_value;
    type_t *type;
    struct tag_s *next;
} tag_t;

typedef struct type_list_s {
    type_t *type;
    struct type_list_s *next;
} type_list_t;

struct type_s {
    enum {
        UnknownType,
        AbortType,
        ReturnType,
        VoidType,
        MemoryType,
        BoolType,
        ByteType,
        BigIntType,
        IntType,
        NumType,
        CStringType,
        MomentType,
        TextType,
        ListType,
        SetType,
        TableType,
        FunctionType,
        ClosureType,
        PointerType,
        StructType,
        EnumType,
        OptionalType,
        TypeInfoType,
        MutexedType,
        ModuleType,
    } tag;

    union {
        struct {
        } UnknownType, AbortType, VoidType, MemoryType, BoolType;
        struct {
            type_t *ret;
        } ReturnType;
        struct {} BigIntType;
        struct {
            enum { TYPE_IBITS8=8, TYPE_IBITS16=16, TYPE_IBITS32=32, TYPE_IBITS64=64 } bits;
        } IntType;
        struct {} ByteType;
        struct {
            enum { TYPE_NBITS32=32, TYPE_NBITS64=64 } bits;
        } NumType;
        struct {} CStringType, MomentType;
        struct {
            const char *lang;
            struct env_s *env;
        } TextType;
        struct {
            type_t *item_type;
        } ListType;
        struct {
            type_t *item_type;
        } SetType;
        struct {
            type_t *key_type, *value_type;
            struct env_s *env;
            ast_t *default_value;
        } TableType;
        struct {
            arg_t *args;
            type_t *ret;
        } FunctionType;
        struct {
            type_t *fn;
        } ClosureType;
        struct {
            type_t *pointed;
            bool is_stack:1;
        } PointerType;
        struct {
            const char *name;
            arg_t *fields;
            struct env_s *env;
            bool opaque:1, external:1;
        } StructType;
        struct {
            const char *name;
            tag_t *tags;
            struct env_s *env;
            bool opaque;
        } EnumType;
        struct {
            type_t *type;
        } OptionalType, MutexedType;
        struct {
            const char *name;
            type_t *type;
            struct env_s *env;
        } TypeInfoType;
        struct {
            const char *name;
        } ModuleType;
    } __data;
};

#define Type(typetag, ...) new(type_t, .tag=typetag, .__data.typetag={__VA_ARGS__})
#define INT_TYPE Type(BigIntType)
#define NUM_TYPE Type(NumType, .bits=TYPE_NBITS64)

int printf_pointer_size(const struct printf_info *info, size_t n, int argtypes[n], int size[n]);
int printf_type(FILE *stream, const struct printf_info *info, const void *const args[]);
CORD type_to_cord(type_t *t);
const char *get_type_name(type_t *t);
PUREFUNC bool type_eq(type_t *a, type_t *b);
PUREFUNC bool type_is_a(type_t *t, type_t *req);
type_t *type_or_type(type_t *a, type_t *b);
type_t *value_type(type_t *a);
typedef enum {NUM_PRECISION_EQUAL, NUM_PRECISION_LESS, NUM_PRECISION_MORE, NUM_PRECISION_INCOMPARABLE} precision_cmp_e;
PUREFUNC precision_cmp_e compare_precision(type_t *a, type_t *b);
PUREFUNC bool has_heap_memory(type_t *t);
PUREFUNC bool has_stack_memory(type_t *t);
PUREFUNC bool can_promote(type_t *actual, type_t *needed);
PUREFUNC const char *enum_single_value_tag(type_t *enum_type, type_t *t);
PUREFUNC bool is_int_type(type_t *t);
PUREFUNC bool is_numeric_type(type_t *t);
PUREFUNC bool is_packed_data(type_t *t);
PUREFUNC size_t type_size(type_t *t);
PUREFUNC size_t type_align(type_t *t);
PUREFUNC size_t unpadded_struct_size(type_t *t);
PUREFUNC type_t *non_optional(type_t *t);
type_t *get_field_type(type_t *t, const char *field_name);
PUREFUNC type_t *get_iterated_type(type_t *t);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
