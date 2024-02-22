// Some basic operations defined on AST nodes, mainly converting to
// strings for debugging.
#include <gc/cord.h>
#include <stdarg.h>
#include <printf.h>

#include "ast.h"
#include "builtins/string.h"

static const char *OP_NAMES[] = {
    [BINOP_UNKNOWN]="unknown",
    [BINOP_POWER]="^", [BINOP_MULT]="*", [BINOP_DIVIDE]="/",
    [BINOP_MOD]="mod", [BINOP_MOD1]="mod1", [BINOP_PLUS]="+", [BINOP_MINUS]="minus",
    [BINOP_CONCAT]="++", [BINOP_LSHIFT]="<<", [BINOP_RSHIFT]=">>", [BINOP_MIN]="min",
    [BINOP_MAX]="max", [BINOP_EQ]="==", [BINOP_NE]="!=", [BINOP_LT]="<",
    [BINOP_LE]="<=", [BINOP_GT]=">", [BINOP_GE]=">=", [BINOP_AND]="and", [BINOP_OR]="or", [BINOP_XOR]="xor",
};

static CORD ast_to_cord(ast_t *ast);
static CORD ast_list_to_cord(ast_list_t *asts);
static CORD type_ast_to_cord(type_ast_t *t);
static CORD arg_list_to_cord(arg_ast_t *args);
static CORD when_clauses_to_cord(when_clause_t *clauses);
static CORD tags_to_cord(tag_ast_t *tags);

#define TO_CORD(x) _Generic(x, \
                            ast_t*: ast_to_cord(x), \
                            ast_list_t*: ast_list_to_cord(x), \
                            type_ast_t*: type_ast_to_cord(x), \
                            arg_ast_list_t*: arg_list_to_cord(x), \
                            tag_ast_t*: tags_to_cord(x), \
                            const char *: x, \
                            int64_t: CORD_asprintf("%ld", x), \
                            unsigned short int: CORD_asprintf("%d", x), \
                            double: CORD_asprintf("%g", x), \
                            bool: CORD_asprintf("%s", x ? "yes" : "no"), \
                            unsigned char: CORD_asprintf("%s", x ? "yes" : "no"))

CORD ast_list_to_cord(ast_list_t *asts)
{
    if (!asts)
        return "\x1b[35mNULL\x1b[m";

    CORD c = "[";
    for (; asts; asts = asts->next) {
        c = CORD_cat(c, ast_to_cord(asts->ast));
        if (asts->next) c = CORD_cat(c, ", ");
    }
    c = CORD_cat(c, "]");
    return c;
}

CORD arg_list_to_cord(arg_ast_t *args) {
    CORD c = "Args(";
    for (; args; args = args->next) {
        if (args->name)
            c = CORD_cat(c, args->name);
        if (args->type)
            CORD_sprintf(&c, "%r:%s", c, type_ast_to_cord(args->type));
        if (args->default_val)
            CORD_sprintf(&c, "%r=%s", c, ast_to_cord(args->default_val));
        if (args->next) c = CORD_cat(c, ", ");
    }
    return CORD_cat(c, ")");
}

CORD when_clauses_to_cord(when_clause_t *clauses) {
    CORD c = "Clauses(";
    for (; clauses; clauses = clauses->next) {
        if (clauses->var) c = CORD_all(c, ast_to_cord(clauses->var), ":");
        c = CORD_all(c, ast_to_cord(clauses->tag_name), "=", ast_to_cord(clauses->body));
        if (clauses->next) c = CORD_cat(c, ", ");
    }
    return CORD_cat(c, ")");
}

CORD tags_to_cord(tag_ast_t *tags) {
    CORD c = "Tags(";
    for (; tags; tags = tags->next) {
        if (tags->name)
            c = CORD_cat(c, tags->name);
        CORD_sprintf(&c, "%r(%r)=%ld", c, arg_list_to_cord(tags->fields), tags->value);
        if (tags->next) c = CORD_cat(c, ", ");
    }
    return CORD_cat(c, ")");
}

CORD ast_to_cord(ast_t *ast)
{
    if (!ast) return "\x1b[35mNULL\x1b[m";

    switch (ast->tag) {
#define T(type, ...) case type: { auto data = ast->__data.type; (void)data; return CORD_asprintf("\x1b[34;1m" #type "\x1b[m" __VA_ARGS__); }
    T(Unknown,  "Unknown")
    T(Nil, "(%r)", type_ast_to_cord(data.type))
    T(Bool, "(\x1b[35m%s\x1b[m)", data.b ? "yes" : "no")
    T(Var, "(\x1b[36;1m%s\x1b[m)", data.name)
    T(Int, "(\x1b[35m%ld\x1b[m, bits=\x1b[35m%ld\x1b[m)", data.i, data.bits)
    T(Num, "(\x1b[35m%ld\x1b[m, bits=\x1b[35m%ld\x1b[m)", data.n, data.bits)
    T(StringLiteral, "\x1b[35m\"%r\"\x1b[m", data.cord)
    T(StringJoin, "(%r)", ast_list_to_cord(data.children))
    T(Declare, "(var=%s, value=%r)", ast_to_cord(data.var), ast_to_cord(data.value))
    T(Assign, "(targets=%r, values=%r)", ast_list_to_cord(data.targets), ast_list_to_cord(data.values))
    T(BinaryOp, "(%r, %s, %r)", ast_to_cord(data.lhs), OP_NAMES[data.op], ast_to_cord(data.rhs))
    T(UpdateAssign, "(%r, %s, %r)", ast_to_cord(data.lhs), OP_NAMES[data.op], ast_to_cord(data.rhs))
    T(Length, "(%r)", ast_to_cord(data.value))
    T(Negative, "(%r)", ast_to_cord(data.value))
    T(Not, "(%r)", ast_to_cord(data.value))
    T(HeapAllocate, "(%r)", ast_to_cord(data.value))
    T(StackReference, "(%r)", ast_to_cord(data.value))
    T(Min, "(%r, %r, key=%r)", ast_to_cord(data.lhs), ast_to_cord(data.rhs), ast_to_cord(data.key))
    T(Max, "(%r, %r, key=%r)", ast_to_cord(data.lhs), ast_to_cord(data.rhs), ast_to_cord(data.key))
    T(Array, "(%r, type=%r)", ast_list_to_cord(data.items), type_ast_to_cord(data.type))
    T(Table, "(key_type=%r, value_type=%r, fallback=%r, default_value=%r, entries=%r)",
      type_ast_to_cord(data.key_type), type_ast_to_cord(data.value_type),
      ast_to_cord(data.fallback), ast_to_cord(data.default_value),
      ast_list_to_cord(data.entries))
    T(TableEntry, "(%r => %r)", ast_to_cord(data.key), ast_to_cord(data.value))
    T(FunctionDef, "(name=%r, args=%r, ret=%r, body=%r)", ast_to_cord(data.name),
      arg_list_to_cord(data.args), type_ast_to_cord(data.ret_type), ast_to_cord(data.body))
    T(Lambda, "(args=%r, body=%r)", arg_list_to_cord(data.args), ast_to_cord(data.body))
    T(FunctionCall, "(fn=%r, args=%r)", ast_to_cord(data.fn), ast_list_to_cord(data.args))
    T(KeywordArg, "(%s=%r)", ast_to_cord(data.arg))
    T(Block, "(%r)", ast_list_to_cord(data.statements))
    T(For, "(index=%r, value=%r, iter=%r, body=%r)", ast_to_cord(data.index), ast_to_cord(data.value),
      ast_to_cord(data.iter), ast_to_cord(data.body))
    T(While, "(condition=%r, body=%r)", ast_to_cord(data.condition), ast_to_cord(data.body))
    T(If, "(condition=%r, body=%r, else=%r)", ast_to_cord(data.condition), ast_to_cord(data.body), ast_to_cord(data.else_body))
    T(When, "(subject=%r, clauses=%r, else=%r)", ast_to_cord(data.subject), when_clauses_to_cord(data.clauses), ast_to_cord(data.else_body))
    T(Reduction, "(iter=%r, combination=%r, fallback=%r)", ast_to_cord(data.iter), ast_to_cord(data.combination), ast_to_cord(data.fallback))
    T(Skip, "(%s)", data.target)
    T(Stop, "(%s)", data.target)
    T(Pass, "")
    T(Return, "(%r)", ast_to_cord(data.value))
    T(Extern, "(name=%s, type=%r)", data.name, type_ast_to_cord(data.type))
    T(StructDef, "(%s, fields=%r, namespace=%r)", data.name, arg_list_to_cord(data.fields), ast_to_cord(data.namespace))
    T(EnumDef, "(%s, tags=%r, namespace=%r)", data.name, tags_to_cord(data.tags), ast_to_cord(data.namespace))
    T(Index, "(indexed=%r, index=%r)", ast_to_cord(data.indexed), ast_to_cord(data.index))
    T(FieldAccess, "(fielded=%r, field=%s)", ast_to_cord(data.fielded), data.field)
    T(DocTest, "(expr=%r, output=%r)", ast_to_cord(data.expr), Str__quoted(data.output, true))
    T(Use, "(%s)", Str__quoted(data.path, true))
    T(LinkerDirective, "(%s)", Str__quoted(data.directive, true))
#undef T
    }
    return NULL;
}

CORD type_ast_to_cord(type_ast_t *t)
{
    if (!t) return "\x1b[35mNULL\x1b[m";

    switch (t->tag) {
#define T(type, ...) case type: { auto data = t->__data.type; (void)data; return CORD_asprintf("\x1b[32;1m" #type "\x1b[m" __VA_ARGS__); }
    T(UnknownTypeAST, "")
    T(VarTypeAST, "(\x1b[36;1m%s\x1b[m)", data.name)
    T(PointerTypeAST, "(%r, is_optional=%d, is_stack=%d, is_readonly=%d)",
      type_ast_to_cord(data.pointed), data.is_optional,
      data.is_stack, data.is_readonly)
    T(ArrayTypeAST, "(%r)", type_ast_to_cord(data.item))
    T(TableTypeAST, "(%r => %r)", type_ast_to_cord(data.key), type_ast_to_cord(data.value))
    T(FunctionTypeAST, "(args=%r, ret=%r)", arg_list_to_cord(data.args), type_ast_to_cord(data.ret))
#undef T
    }
    return NULL;
}

const char *ast_to_str(ast_t *ast) {
    CORD c = ast_to_cord(ast);
    return CORD_to_char_star(c);
}

const char *type_ast_to_str(type_ast_t *t) {
    CORD c = type_ast_to_cord(t);
    return CORD_to_char_star(c);
}

int printf_ast(FILE *stream, const struct printf_info *info, const void *const args[])
{
    ast_t *ast = *(ast_t**)(args[0]);
    if (ast) {
        if (info->alt)
            return fprintf(stream, "%.*s", (int)(ast->end - ast->start), ast->start);
        else
            return fprintf(stream, "%s", ast_to_str(ast));
    } else {
        return fputs("(null)", stream);
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
