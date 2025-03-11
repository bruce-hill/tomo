#pragma once

// Logic defining ASTs (abstract syntax trees) to represent code

#include <err.h>
#include <gc/cord.h>
#include <printf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "stdlib/datatypes.h"
#include "stdlib/files.h"
#include "stdlib/util.h"

#define NewAST(_file, _start, _end, ast_tag, ...) (new(ast_t, .file=_file, .start=_start, .end=_end,\
                                                     .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define NewTypeAST(_file, _start, _end, ast_tag, ...) (new(type_ast_t, .file=_file, .start=_start, .end=_end,\
                                                     .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define FakeAST(ast_tag, ...) (new(ast_t, .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define WrapAST(ast, ast_tag, ...) (new(ast_t, .file=(ast)->file, .start=(ast)->start, .end=(ast)->end, .tag=ast_tag, .__data.ast_tag={__VA_ARGS__}))
#define TextAST(ast, _str) WrapAST(ast, TextLiteral, .str=GC_strdup(_str))
#define Match(x, _tag) ((x)->tag == _tag ? &(x)->__data._tag : (errx(1, __FILE__ ":%d This was supposed to be a " # _tag "\n", __LINE__), &(x)->__data._tag))

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

struct binding_s;
typedef struct type_ast_s type_ast_t;
typedef struct ast_s ast_t;

typedef struct ast_list_s {
    ast_t *ast;
    struct ast_list_s *next;
} ast_list_t;

typedef struct arg_ast_s {
    const char *name;
    type_ast_t *type;
    ast_t *value;
    struct arg_ast_s *next;
} arg_ast_t;

typedef struct when_clause_s {
    ast_t *pattern, *body;
    struct when_clause_s *next;
} when_clause_t;

typedef enum {
    BINOP_UNKNOWN,
    BINOP_POWER=100, BINOP_MULT, BINOP_DIVIDE, BINOP_MOD, BINOP_MOD1, BINOP_PLUS,
    BINOP_MINUS, BINOP_CONCAT, BINOP_LSHIFT, BINOP_ULSHIFT, BINOP_RSHIFT, BINOP_URSHIFT, BINOP_MIN,
    BINOP_MAX, BINOP_EQ, BINOP_NE, BINOP_LT, BINOP_LE, BINOP_GT, BINOP_GE,
    BINOP_CMP,
    BINOP_AND, BINOP_OR, BINOP_XOR,
} binop_e;

extern const char *binop_method_names[BINOP_XOR+1];

typedef enum {
    UnknownTypeAST,
    VarTypeAST,
    PointerTypeAST,
    ArrayTypeAST,
    SetTypeAST,
    TableTypeAST,
    FunctionTypeAST,
    OptionalTypeAST,
    MutexedTypeAST,
} type_ast_e;

typedef struct tag_ast_s {
    const char *name;
    arg_ast_t *fields;
    bool secret:1;
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
            bool is_stack:1;
        } PointerTypeAST;
        struct {
            type_ast_t *item;
        } ArrayTypeAST;
        struct {
            type_ast_t *key, *value;
            ast_t *default_value;
        } TableTypeAST;
        struct {
            type_ast_t *item;
        } SetTypeAST;
        struct {
            arg_ast_t *args;
            type_ast_t *ret;
        } FunctionTypeAST;
        struct {
            type_ast_t *type;
        } OptionalTypeAST, MutexedTypeAST;
    } __data;
};

typedef enum {
    Unknown = 0,
    None, Bool, Var,
    Int, Num,
    TextLiteral, TextJoin, PrintStatement,
    Declare, Assign,
    BinaryOp, UpdateAssign,
    Not, Negative, HeapAllocate, StackReference, Mutexed, Holding,
    Min, Max,
    Array, Set, Table, TableEntry, Comprehension,
    FunctionDef, Lambda, ConvertDef,
    FunctionCall, MethodCall,
    Block,
    For, While, If, When, Repeat,
    Reduction,
    Skip, Stop, Pass,
    Defer,
    Return,
    Extern,
    StructDef, EnumDef, LangDef,
    Index, FieldAccess, Optional, NonOptional,
    Moment,
    DocTest,
    Use,
    InlineCCode,
    Deserialize,
} ast_e;

struct ast_s {
    ast_e tag;
    file_t *file;
    const char *start, *end;
    union {
        struct {} Unknown;
        struct {
            type_ast_t *type;
        } None;
        struct {
            bool b;
        } Bool;
        struct {
            const char *name;
        } Var;
        struct {
            const char *str;
        } Int;
        struct {
            double n;
        } Num;
        struct {
            CORD cord;
        } TextLiteral;
        struct {
            const char *lang;
            ast_list_t *children;
        } TextJoin;
        struct {
            ast_list_t *to_print;
        } PrintStatement;
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
        } Not, Negative, HeapAllocate, StackReference, Mutexed;
        struct {
            ast_t *mutexed, *body;
        } Holding;
        struct {
            ast_t *lhs, *rhs, *key;
        } Min, Max;
        struct {
            type_ast_t *item_type;
            ast_list_t *items;
        } Array;
        struct {
            type_ast_t *item_type;
            ast_list_t *items;
        } Set;
        struct {
            type_ast_t *key_type, *value_type;
            ast_t *default_value;
            ast_t *fallback;
            ast_list_t *entries;
        } Table;
        struct {
            ast_t *key, *value;
        } TableEntry;
        struct {
            ast_list_t *vars;
            ast_t *expr, *iter, *filter;
        } Comprehension;
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
            type_ast_t *ret_type;
            ast_t *body;
            ast_t *cache;
            bool is_inline;
        } ConvertDef;
        struct {
            arg_ast_t *args;
            type_ast_t *ret_type;
            ast_t *body;
            int64_t id;
        } Lambda;
        struct {
            ast_t *fn;
            arg_ast_t *args;
        } FunctionCall;
        struct {
            const char *name;
            ast_t *self;
            arg_ast_t *args;
        } MethodCall;
        struct {
            ast_list_t *statements;
        } Block;
        struct {
            ast_list_t *vars;
            ast_t *iter, *body, *empty;
        } For;
        struct {
            ast_t *condition, *body;
        } While;
        struct {
            ast_t *body;
        } Repeat;
        struct {
            ast_t *condition, *body, *else_body;
        } If;
        struct {
            ast_t *subject;
            when_clause_t *clauses;
            ast_t *else_body;
        } When;
        struct {
            ast_t *iter, *key;
            binop_e op;
        } Reduction;
        struct {
            const char *target;
        } Skip, Stop;
        struct {} Pass;
        struct {
            ast_t *body;
        } Defer;
        struct {
            ast_t *value;
        } Return;
        struct {
            const char *name;
            type_ast_t *type;
        } Extern;
        struct {
            const char *name;
            arg_ast_t *fields;
            ast_t *namespace;
            bool secret:1, external:1;
        } StructDef;
        struct {
            const char *name;
            tag_ast_t *tags;
            ast_t *namespace;
        } EnumDef;
        struct {
            const char *name;
            ast_t *namespace;
        } LangDef;
        struct {
            ast_t *indexed, *index;
            bool unchecked;
        } Index;
        struct {
            ast_t *fielded;
            const char *field;
        } FieldAccess;
        struct {
            ast_t *value;
        } Optional, NonOptional;
        struct {
            Moment_t moment;
        } Moment;
        struct {
            ast_t *expr;
            const char *output;
            bool skip_source:1;
        } DocTest;
        struct {
            ast_t *var;
            const char *path;
            enum { USE_LOCAL, USE_MODULE, USE_SHARED_OBJECT, USE_HEADER, USE_C_CODE, USE_ASM } what;
        } Use;
        struct {
            CORD code;
            struct type_s *type;
            type_ast_t *type_ast;
        } InlineCCode;
        struct {
            ast_t *value;
            type_ast_t *type;
        } Deserialize;
    } __data;
};

CORD ast_to_xml(ast_t *ast);
CORD type_ast_to_xml(type_ast_t *ast);
int printf_ast(FILE *stream, const struct printf_info *info, const void *const args[]);
PUREFUNC bool is_idempotent(ast_t *ast);
void visit_topologically(ast_list_t *ast, Closure_t fn);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
