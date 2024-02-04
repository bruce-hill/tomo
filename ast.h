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

typedef struct {
    const char *name;
    struct binding_s *binding;
} var_t;

typedef struct ast_list_s {
    ast_t *ast;
    struct ast_list_s *next;
} ast_list_t;

typedef struct arg_list_s {
    var_t var;
    type_ast_t *type;
    ast_t *default_val;
    struct arg_list_s *next;
} arg_list_t;

#define REVERSE_LIST(list) do { \
    __typeof(list) _prev = NULL; \
    __typeof(list) _next = NULL; \
    auto _current = list; \
    while (_current != NULL) { \
        _next = _current->next; \
        _current->next = _prev; \
        _prev = _current; \
        _current = _next; \
    } \
    list = _prev; \
} while(0)

typedef enum {
    UNOP_UNKNOWN,
    UNOP_NOT=1, UNOP_NEGATIVE,
    UNOP_HEAP_ALLOCATE,
    UNOP_STACK_REFERENCE,
} unop_e;

typedef enum {
    BINOP_UNKNOWN,
    BINOP_POWER=100, BINOP_MULT, BINOP_DIVIDE, BINOP_MOD, BINOP_MOD1, BINOP_PLUS,
    BINOP_MINUS, BINOP_CONCAT, BINOP_LSHIFT, BINOP_RSHIFT, BINOP_MIN,
    BINOP_MAX, BINOP_EQ, BINOP_NE, BINOP_LT, BINOP_LE, BINOP_GT, BINOP_GE,
    BINOP_AND, BINOP_OR, BINOP_XOR,
} binop_e;

typedef enum {
    TypeUnknown,
    TypeVar,
    TypePointer,
    TypeStruct,
    TypeTaggedUnion,
    TypeArray,
    TypeTable,
    TypeFunction,
} type_ast_e;

typedef struct tag_s {
    const char *name;
    struct type_ast_s *type;
    int64_t value;
    struct tag_s *next;
} tag_t;

struct type_ast_s {
    type_ast_e tag;
    sss_file_t *file;
    const char *start, *end;
    union {
        struct {} TypeUnknown;
        struct {
            var_t var;
        } TypeVar;
        struct {
            type_ast_t *pointed;
            bool is_optional:1, is_stack:1, is_readonly:1;
        } TypePointer;
        struct {
            arg_list_t *fields;
        } TypeStruct;
        struct {
            tag_t *tags;
        } TypeTaggedUnion;
        struct {
            type_ast_t *item;
        } TypeArray;
        struct {
            type_ast_t *key, *value;
        } TypeTable;
        struct {
            arg_list_t *args;
            type_ast_t *ret;
        } TypeFunction;
    } __data;
};

typedef enum {
    Unknown = 0,
    Nil, Bool, Var,
    Int, Num, Char,
    StringLiteral, StringJoin, Interp,
    Declare, Assign,
    BinaryOp, UnaryOp, UpdateAssign,
    Min, Max,
    Array, Table, TableEntry,
    FunctionDef, Lambda,
    FunctionCall, KeywordArg,
    Block,
    For, While, If,
    Reduction,
    Skip, Stop, Pass,
    Return,
    Extern,
    TypeDef,
    Index, FieldAccess,
    DocTest,
    Use,
    LinkerDirective,
} ast_e;

struct ast_s {
    ast_e tag;
    sss_file_t *file;
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
            var_t var;
        } Var;
        struct {
            int64_t i;
            enum { INT_64BIT, INT_32BIT, INT_16BIT, INT_8BIT } precision;
        } Int;
        struct {
            double n;
            enum { NUM_64BIT, NUM_32BIT } precision;
        } Num;
        struct {
            char c;
        } Char;
        struct {
            const char *str;
        } StringLiteral;
        struct {
            ast_list_t *children;
        } StringJoin;
        struct {
            ast_t *value;
            bool labelled:1, colorize:1, quote_string:1;
        } Interp;
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
            unop_e op;
            ast_t *value;
        } UnaryOp;
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
            arg_list_t *args;
            type_ast_t *ret_type;
            ast_t *body;
            ast_t *cache;
            bool is_inline;
        } FunctionDef;
        struct {
            arg_list_t *args;
            ast_t *body;
        } Lambda;
        struct {
            ast_t *fn;
            ast_list_t *args;
            type_ast_t *extern_return_type;
        } FunctionCall;
        struct {
            const char *name;
            ast_t *arg;
        } KeywordArg;
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
            var_t var;
            type_ast_t *type;
            ast_t *namespace;
        } TypeDef;
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
            sss_file_t *file;
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
