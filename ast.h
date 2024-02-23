#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <printf.h>

#include "files.h"
#include "util.h"

#define NewAST(_file, _start, _end, ast_tag, ...) (new(ast_t, .file=_file, .start=_start, .end=_end,\
                                                     .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define NewTypeAST(_file, _start, _end, ast_tag, ...) (new(type_ast_t, .file=_file, .start=_start, .end=_end,\
                                                     .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define FakeAST(ast_tag, ...) (new(ast_t, .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define WrapAST(ast, ast_tag, ...) (new(ast_t, .file=(ast)->file, .start=(ast)->start, .end=(ast)->end, .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define StringAST(ast, _str) WrapAST(ast, StringLiteral, .str=heap_str(_str))

struct binding_s;
typedef struct type_ast_s type_ast_t;
typedef struct ast_s ast_t;

typedef struct ast_list_s {
    ast_t *ast;
    struct ast_list_s *next;
} ast_list_t;

typedef struct when_clause_s {
    ast_t *var, *tag_name, *body;
    struct when_clause_s *next;
} when_clause_t;

typedef struct arg_ast_s {
    const char *name;
    type_ast_t *type;
    ast_t *value;
    struct arg_ast_s *next;
} arg_ast_t;

typedef enum {
    BINOP_UNKNOWN,
    BINOP_POWER=100, BINOP_MULT, BINOP_DIVIDE, BINOP_MOD, BINOP_MOD1, BINOP_PLUS,
    BINOP_MINUS, BINOP_CONCAT, BINOP_LSHIFT, BINOP_RSHIFT, BINOP_MIN,
    BINOP_MAX, BINOP_EQ, BINOP_NE, BINOP_LT, BINOP_LE, BINOP_GT, BINOP_GE,
    BINOP_AND, BINOP_OR, BINOP_XOR,
} binop_e;

typedef enum {
    UnknownTypeAST,
    VarTypeAST,
    PointerTypeAST,
    ArrayTypeAST,
    TableTypeAST,
    FunctionTypeAST,
} type_ast_e;

typedef struct tag_ast_s {
    const char *name;
    arg_ast_t *fields;
    int64_t value;
    struct tag_ast_s *next;
} tag_ast_t;

struct type_ast_s {
    type_ast_e tag;
    file_t *file;
    const char *start, *end;
    union {
        struct {} UnknownTypeAST;
        struct {
            const char *name;
        } VarTypeAST;
        struct {
            type_ast_t *pointed;
            bool is_optional:1, is_stack:1, is_readonly:1;
        } PointerTypeAST;
        struct {
            type_ast_t *item;
        } ArrayTypeAST;
        struct {
            type_ast_t *key, *value;
        } TableTypeAST;
        struct {
            arg_ast_t *args;
            type_ast_t *ret;
        } FunctionTypeAST;
    } __data;
};

typedef enum {
    Unknown = 0,
    Nil, Bool, Var,
    Int, Num,
    StringLiteral, StringJoin,
    Declare, Assign,
    BinaryOp, UpdateAssign,
    Length, Not, Negative, HeapAllocate, StackReference,
    Min, Max,
    Array, Table, TableEntry,
    FunctionDef, Lambda,
    FunctionCall,
    Block,
    For, While, If, When,
    Reduction,
    Skip, Stop, Pass,
    Return,
    Extern,
    StructDef,
    EnumDef,
    Index, FieldAccess,
    DocTest,
    Use,
    LinkerDirective,
} ast_e;

struct ast_s {
    ast_e tag;
    file_t *file;
    const char *start, *end;
    union {
        struct {} Unknown;
        struct {
            type_ast_t *type;
        } Nil;
        struct {
            bool b;
        } Bool;
        struct {
            const char *name;
        } Var;
        struct {
            int64_t i;
            int64_t bits;
        } Int;
        struct {
            double n;
            int64_t bits;
        } Num;
        struct {
            CORD cord;
        } StringLiteral;
        struct {
            ast_list_t *children;
        } StringJoin;
        struct {
            ast_t *var;
            ast_t *value;
        } Declare;
        struct {
            ast_list_t *targets, *values;
        } Assign;
        struct {
            ast_t *lhs;
            binop_e op;
            ast_t *rhs;
        } BinaryOp, UpdateAssign;
        struct {
            ast_t *value;
        } Length, Not, Negative, HeapAllocate, StackReference;
        struct {
            ast_t *lhs, *rhs, *key;
        } Min, Max;
        struct {
            type_ast_t *type;
            ast_list_t *items;
        } Array;
        struct {
            type_ast_t *key_type, *value_type;
            ast_t *fallback, *default_value;
            ast_list_t *entries;
        } Table;
        struct {
            ast_t *key, *value;
        } TableEntry;
        struct {
            ast_t *name;
            arg_ast_t *args;
            type_ast_t *ret_type;
            ast_t *body;
            ast_t *cache;
            bool is_inline;
        } FunctionDef;
        struct {
            arg_ast_t *args;
            ast_t *body;
        } Lambda;
        struct {
            ast_t *fn;
            arg_ast_t *args;
            type_ast_t *extern_return_type;
        } FunctionCall;
        struct {
            ast_list_t *statements;
        } Block;
        struct {
            ast_t *index, *value, *iter, *body;
        } For;
        struct {
            ast_t *condition, *body;
        } While;
        struct {
            ast_t *condition, *body, *else_body;
        } If;
        struct {
            ast_t *subject;
            when_clause_t *clauses;
            ast_t *else_body;
        } When;
        struct {
            ast_t *iter, *combination, *fallback;
        } Reduction;
        struct {
            const char *target;
        } Skip, Stop;
        struct {} Pass;
        struct {
            ast_t *value;
        } Return;
        struct {
            const char *name;
            type_ast_t *type;
            bool address;
        } Extern;
        struct {
            const char *name;
            arg_ast_t *fields;
            ast_t *namespace;
            bool secret:1;
        } StructDef;
        struct {
            const char *name;
            tag_ast_t *tags;
            ast_t *namespace;
        } EnumDef;
        struct {
            ast_t *indexed, *index;
            bool unchecked;
        } Index;
        struct {
            ast_t *fielded;
            const char *field;
        } FieldAccess;
        struct {
            ast_t *expr;
            const char *output;
            bool skip_source:1;
        } DocTest;
        struct {
            const char *path;
            file_t *file;
            bool main_program;
        } Use;
        struct {
            const char *directive;
        } LinkerDirective;
    } __data;
};

const char *ast_to_str(ast_t *ast);
const char *type_ast_to_str(type_ast_t *ast);
int printf_ast(FILE *stream, const struct printf_info *info, const void *const args[]);
ast_list_t *get_ast_children(ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
