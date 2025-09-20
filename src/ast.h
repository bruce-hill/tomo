// Logic defining ASTs (abstract syntax trees) to represent code

#pragma once

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "stdlib/datatypes.h"
#include "stdlib/files.h"
#include "stdlib/util.h"

#define NewAST(_file, _start, _end, ast_tag, ...)                                                                      \
    (new (ast_t, .file = _file, .start = _start, .end = _end, .tag = ast_tag, .__data.ast_tag = {__VA_ARGS__}))
#define NewTypeAST(_file, _start, _end, ast_tag, ...)                                                                  \
    (new (type_ast_t, .file = _file, .start = _start, .end = _end, .tag = ast_tag, .__data.ast_tag = {__VA_ARGS__}))
#define FakeAST(ast_tag, ...) (new (ast_t, .tag = ast_tag, .__data.ast_tag = {__VA_ARGS__}))
#define WrapAST(ast, ast_tag, ...)                                                                                     \
    (new (ast_t, .file = (ast)->file, .start = (ast)->start, .end = (ast)->end, .tag = ast_tag,                        \
          .__data.ast_tag = {__VA_ARGS__}))
#define TextAST(ast, _str) WrapAST(ast, TextLiteral, .str = GC_strdup(_str))
#define LiteralCode(code, ...)                                                                                         \
    new (ast_t, .tag = InlineCCode,                                                                                    \
         .__data.InlineCCode = {.chunks = new (ast_list_t, .ast = FakeAST(TextLiteral, code)), __VA_ARGS__})
#define Match(x, _tag)                                                                                                 \
    ((x)->tag == _tag ? &(x)->__data._tag                                                                              \
                      : (errx(1, __FILE__ ":%d This was supposed to be a " #_tag "\n", __LINE__), &(x)->__data._tag))
#define DeclareMatch(var, x, _tag) __typeof((x)->__data._tag) *var = Match(x, _tag)
#define BINARY_OPERANDS(ast)                                                                                           \
    ({                                                                                                                 \
        if (!is_binary_operation(ast)) errx(1, __FILE__ ":%d This is not a binary operation!", __LINE__);              \
        (ast)->__data.Plus;                                                                                            \
    })
#define UPDATE_OPERANDS(ast)                                                                                           \
    ({                                                                                                                 \
        if (!is_update_assignment(ast)) errx(1, __FILE__ ":%d This is not an update assignment!", __LINE__);           \
        (ast)->__data.PlusUpdate;                                                                                      \
    })

#define REVERSE_LIST(list)                                                                                             \
    do {                                                                                                               \
        __typeof(list) _prev = NULL;                                                                                   \
        __typeof(list) _next = NULL;                                                                                   \
        __typeof(list) _current = list;                                                                                \
        while (_current != NULL) {                                                                                     \
            _next = _current->next;                                                                                    \
            _current->next = _prev;                                                                                    \
            _prev = _current;                                                                                          \
            _current = _next;                                                                                          \
        }                                                                                                              \
        list = _prev;                                                                                                  \
    } while (0)

struct binding_s;
typedef struct type_ast_s type_ast_t;
typedef struct ast_s ast_t;
typedef struct {
    ast_t *lhs, *rhs;
} binary_operands_t;

typedef struct ast_list_s {
    ast_t *ast;
    struct ast_list_s *next;
} ast_list_t;

typedef struct arg_ast_s {
    file_t *file;
    const char *start, *end;
    const char *name, *alias;
    type_ast_t *type;
    ast_t *value;
    struct arg_ast_s *next;
} arg_ast_t;

typedef struct when_clause_s {
    ast_t *pattern, *body;
    struct when_clause_s *next;
} when_clause_t;

typedef enum {
    UnknownTypeAST,
    VarTypeAST,
    PointerTypeAST,
    ListTypeAST,
    SetTypeAST,
    TableTypeAST,
    FunctionTypeAST,
    OptionalTypeAST,
    EnumTypeAST,
} type_ast_e;

typedef struct tag_ast_s {
    const char *start, *end;
    const char *name;
    arg_ast_t *fields;
    struct tag_ast_s *next;
    bool secret : 1;
} tag_ast_t;

struct type_ast_s {
    type_ast_e tag;
    file_t *file;
    const char *start, *end;
    union {
        struct {
        } UnknownTypeAST;
        struct {
            const char *name;
        } VarTypeAST;
        struct {
            type_ast_t *pointed;
            bool is_stack : 1;
        } PointerTypeAST;
        struct {
            type_ast_t *item;
        } ListTypeAST;
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
        } OptionalTypeAST;
        struct {
            Text_t name;
            tag_ast_t *tags;
        } EnumTypeAST;
    } __data;
};

#define BINOP_CASES                                                                                                    \
    Power:                                                                                                             \
    case Multiply:                                                                                                     \
    case Divide:                                                                                                       \
    case Mod:                                                                                                          \
    case Mod1:                                                                                                         \
    case Plus:                                                                                                         \
    case Minus:                                                                                                        \
    case Concat:                                                                                                       \
    case LeftShift:                                                                                                    \
    case UnsignedLeftShift:                                                                                            \
    case RightShift:                                                                                                   \
    case UnsignedRightShift:                                                                                           \
    case Equals:                                                                                                       \
    case NotEquals:                                                                                                    \
    case LessThan:                                                                                                     \
    case LessThanOrEquals:                                                                                             \
    case GreaterThan:                                                                                                  \
    case GreaterThanOrEquals:                                                                                          \
    case Compare:                                                                                                      \
    case And:                                                                                                          \
    case Or:                                                                                                           \
    case Xor:                                                                                                          \
    case PowerUpdate:                                                                                                  \
    case MultiplyUpdate:                                                                                               \
    case DivideUpdate:                                                                                                 \
    case ModUpdate:                                                                                                    \
    case Mod1Update:                                                                                                   \
    case PlusUpdate:                                                                                                   \
    case MinusUpdate:                                                                                                  \
    case ConcatUpdate:                                                                                                 \
    case LeftShiftUpdate:                                                                                              \
    case UnsignedLeftShiftUpdate:                                                                                      \
    case RightShiftUpdate:                                                                                             \
    case UnsignedRightShiftUpdate:                                                                                     \
    case AndUpdate:                                                                                                    \
    case OrUpdate:                                                                                                     \
    case XorUpdate
#define UPDATE_CASES                                                                                                   \
    PowerUpdate:                                                                                                       \
    case MultiplyUpdate:                                                                                               \
    case DivideUpdate:                                                                                                 \
    case ModUpdate:                                                                                                    \
    case Mod1Update:                                                                                                   \
    case PlusUpdate:                                                                                                   \
    case MinusUpdate:                                                                                                  \
    case ConcatUpdate:                                                                                                 \
    case LeftShiftUpdate:                                                                                              \
    case UnsignedLeftShiftUpdate:                                                                                      \
    case RightShiftUpdate:                                                                                             \
    case UnsignedRightShiftUpdate:                                                                                     \
    case AndUpdate:                                                                                                    \
    case OrUpdate:                                                                                                     \
    case XorUpdate

typedef enum {
    Unknown = 0,
    None,
    Bool,
    Var,
    Int,
    Num,
    TextLiteral,
    TextJoin,
    Path,
    Declare,
    Assign,
    Power,
    Multiply,
    Divide,
    Mod,
    Mod1,
    Plus,
    Minus,
    Concat,
    LeftShift,
    UnsignedLeftShift,
    RightShift,
    UnsignedRightShift,
    Equals,
    NotEquals,
    LessThan,
    LessThanOrEquals,
    GreaterThan,
    GreaterThanOrEquals,
    Compare,
    And,
    Or,
    Xor,
    PowerUpdate,
    MultiplyUpdate,
    DivideUpdate,
    ModUpdate,
    Mod1Update,
    PlusUpdate,
    MinusUpdate,
    ConcatUpdate,
    LeftShiftUpdate,
    UnsignedLeftShiftUpdate,
    RightShiftUpdate,
    UnsignedRightShiftUpdate,
    AndUpdate,
    OrUpdate,
    XorUpdate,
    Not,
    Negative,
    HeapAllocate,
    StackReference,
    Min,
    Max,
    List,
    Set,
    Table,
    TableEntry,
    Comprehension,
    FunctionDef,
    Lambda,
    ConvertDef,
    FunctionCall,
    MethodCall,
    Block,
    For,
    While,
    If,
    When,
    Repeat,
    Reduction,
    Skip,
    Stop,
    Pass,
    Defer,
    Return,
    Extern,
    StructDef,
    EnumDef,
    LangDef,
    Index,
    FieldAccess,
    Optional,
    NonOptional,
    DocTest,
    Assert,
    Use,
    InlineCCode,
    Deserialize,
    Extend,
    ExplicitlyTyped,
} ast_e;
#define NUM_AST_TAGS (ExplicitlyTyped + 1)

struct ast_s {
    ast_e tag;
    file_t *file;
    const char *start, *end;
    union {
        struct {
        } Unknown;
        struct {
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
            Text_t text;
        } TextLiteral;
        struct {
            const char *lang;
            ast_list_t *children;
            bool colorize : 1;
        } TextJoin;
        struct {
            const char *path;
        } Path;
        struct {
            ast_t *var;
            type_ast_t *type;
            ast_t *value;
            bool top_level : 1;
        } Declare;
        struct {
            ast_list_t *targets, *values;
        } Assign;
        binary_operands_t Power, Multiply, Divide, Mod, Mod1, Plus, Minus, Concat, LeftShift, UnsignedLeftShift,
            RightShift, UnsignedRightShift, Equals, NotEquals, LessThan, LessThanOrEquals, GreaterThan,
            GreaterThanOrEquals, Compare, And, Or, Xor, PowerUpdate, MultiplyUpdate, DivideUpdate, ModUpdate,
            Mod1Update, PlusUpdate, MinusUpdate, ConcatUpdate, LeftShiftUpdate, UnsignedLeftShiftUpdate,
            RightShiftUpdate, UnsignedRightShiftUpdate, AndUpdate, OrUpdate, XorUpdate;
        struct {
            ast_t *value;
        } Not, Negative, HeapAllocate, StackReference;
        struct {
            ast_t *lhs, *rhs, *key;
        } Min, Max;
        struct {
            ast_list_t *items;
        } List;
        struct {
            ast_list_t *items;
        } Set;
        struct {
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
            ast_e op;
        } Reduction;
        struct {
            const char *target;
        } Skip, Stop;
        struct {
        } Pass;
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
            bool secret : 1, external : 1, opaque : 1;
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
        } Index;
        struct {
            ast_t *fielded;
            const char *field;
        } FieldAccess;
        struct {
            ast_t *value;
        } Optional, NonOptional;
        struct {
            ast_t *expr, *expected;
            bool skip_source : 1;
        } DocTest;
        struct {
            ast_t *expr, *message;
        } Assert;
        struct {
            ast_t *var;
            const char *path;
            enum { USE_LOCAL, USE_MODULE, USE_SHARED_OBJECT, USE_HEADER, USE_C_CODE, USE_ASM } what;
        } Use;
        struct {
            ast_list_t *chunks;
            struct type_s *type;
            type_ast_t *type_ast;
        } InlineCCode;
        struct {
            ast_t *value;
            type_ast_t *type;
        } Deserialize;
        struct {
            const char *name;
            ast_t *body;
        } Extend;
        struct {
            ast_t *ast;
            struct type_s *type;
        } ExplicitlyTyped;
    } __data;
};

extern const int op_tightness[NUM_AST_TAGS];

typedef struct {
    const char *method_name;
    const char *operator;
} binop_info_t;

extern const binop_info_t binop_info[NUM_AST_TAGS];

OptionalText_t ast_source(ast_t *ast);

Text_t ast_to_sexp(ast_t *ast);
const char *ast_to_sexp_str(ast_t *ast);
Text_t type_ast_to_sexp(type_ast_t *ast);

PUREFUNC bool is_idempotent(ast_t *ast);
void visit_topologically(ast_list_t *ast, Closure_t fn);
CONSTFUNC bool is_update_assignment(ast_t *ast);
CONSTFUNC ast_e binop_tag(ast_e tag);
CONSTFUNC bool is_binary_operation(ast_t *ast);
