#pragma once
#include <libgccjit.h>
#include <printf.h>
#include <stdlib.h>
#include <libgccjit.h>

#include "ast.h"
#include "builtins/array.h"

typedef struct type_s type_t;

typedef struct arg_s {
    const char *name;
    type_t *type;
    ast_t *default_val;
    struct arg_s *next;
} arg_t;

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
        VoidType,
        MemoryType,
        BoolType,
        IntType,
        NumType,
        StringType,
        ArrayType,
        TableType,
        FunctionType,
        PointerType,
        StructType,
        EnumType,
        TypeInfoType,
        PlaceholderType,
    } tag;

    union {
        struct {
        } UnknownType, AbortType, VoidType, MemoryType, BoolType;
        struct {
            int64_t bits;
        } IntType;
        struct {
            int64_t bits;
        } NumType;
        struct {
            const char *dsl;
        } StringType;
        struct {
            type_t *item_type;
        } ArrayType;
        struct {
            type_t *key_type, *value_type;
        } TableType;
        struct {
            arg_t *args;
            type_t *ret;
        } FunctionType;
        struct {
            type_t *pointed;
            bool is_optional:1, is_stack:1, is_readonly:1;
        } PointerType;
        struct {
            const char *name;
            arg_t *fields;
        } StructType;
        struct {
            const char *name;
            tag_t *tags;
        } EnumType;
        struct {} TypeInfoType;
        struct {
            const char *filename, *name;
        } PlaceholderType;
    } __data;
};

#define Type(typetag, ...) new(type_t, .tag=typetag, .__data.typetag={__VA_ARGS__})
#define INT_TYPE Type(IntType, .bits=64)
#define NUM_TYPE Type(NumType, .bits=64)

int printf_pointer_size(const struct printf_info *info, size_t n, int argtypes[n], int size[n]);
int printf_type(FILE *stream, const struct printf_info *info, const void *const args[]);
const char* type_to_string_concise(type_t *t);
const char* type_to_typeof_string(type_t *t);
const char* type_to_string(type_t *t);
bool type_eq(type_t *a, type_t *b);
bool type_is_a(type_t *t, type_t *req);
type_t *type_or_type(type_t *a, type_t *b);
type_t *value_type(type_t *a);
typedef enum {NUM_PRECISION_EQUAL, NUM_PRECISION_LESS, NUM_PRECISION_MORE, NUM_PRECISION_INCOMPARABLE} precision_cmp_e;
precision_cmp_e compare_precision(type_t *a, type_t *b);
bool is_orderable(type_t *t);
bool has_heap_memory(type_t *t);
bool has_stack_memory(type_t *t);
bool can_promote(type_t *actual, type_t *needed);
bool can_leave_uninitialized(type_t *t);
bool can_have_cycles(type_t *t);
type_t *table_entry_type(type_t *table_t);
type_t *replace_type(type_t *t, type_t *target, type_t *replacement);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
