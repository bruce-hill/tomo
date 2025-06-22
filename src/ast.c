// Some basic operations defined on AST nodes, mainly converting to
// strings for debugging.
#include <gc/cord.h>
#include <stdarg.h>

#include "ast.h"
#include "types.h"
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "cordhelpers.h"

CONSTFUNC const char *binop_method_name(ast_e tag) {
    switch (tag) {
    case Power: case PowerUpdate: return "power";
    case Multiply: case MultiplyUpdate: return "times";
    case Divide: case DivideUpdate: return "divided_by";
    case Mod: case ModUpdate: return "modulo";
    case Mod1: case Mod1Update: return "modulo1";
    case Plus: case PlusUpdate: return "plus";
    case Minus: case MinusUpdate: return "minus";
    case Concat: case ConcatUpdate: return "concatenated_with";
    case LeftShift: case LeftShiftUpdate: return "left_shifted";
    case RightShift: case RightShiftUpdate: return "right_shifted";
    case UnsignedLeftShift: case UnsignedLeftShiftUpdate: return "unsigned_left_shifted";
    case UnsignedRightShift: case UnsignedRightShiftUpdate: return "unsigned_right_shifted";
    case And: case AndUpdate: return "bit_and";
    case Or: case OrUpdate: return "bit_or";
    case Xor: case XorUpdate: return "bit_xor";
    default: return NULL;
    }
};

CONSTFUNC const char *binop_operator(ast_e tag) {
    switch (tag) {
    case Multiply: case MultiplyUpdate: return "*";
    case Divide: case DivideUpdate: return "/";
    case Mod: case ModUpdate: return "%";
    case Plus: case PlusUpdate: return "+";
    case Minus: case MinusUpdate: return "-";
    case LeftShift: case LeftShiftUpdate: return "<<";
    case RightShift: case RightShiftUpdate: return ">>";
    case And: case AndUpdate: return "&";
    case Or: case OrUpdate: return "|";
    case Xor: case XorUpdate: return "^";
    case Equals: return "==";
    case NotEquals: return "!=";
    case LessThan: return "<";
    case LessThanOrEquals: return "<=";
    case GreaterThan: return ">";
    case GreaterThanOrEquals: return ">=";
    default: return NULL;
    }
};

static CORD ast_list_to_sexp(ast_list_t *asts);
static CORD arg_list_to_sexp(arg_ast_t *args);
static CORD arg_defs_to_sexp(arg_ast_t *args);
static CORD when_clauses_to_sexp(when_clause_t *clauses);
static CORD tags_to_sexp(tag_ast_t *tags);
static CORD optional_sexp(const char *tag, ast_t *ast);
static CORD optional_type_sexp(const char *tag, type_ast_t *ast);

CORD ast_list_to_sexp(ast_list_t *asts)
{
    CORD c = CORD_EMPTY;
    for (; asts; asts = asts->next) {
        c = CORD_all(c, " ", ast_to_sexp(asts->ast));
    }
    return c;
}

CORD arg_defs_to_sexp(arg_ast_t *args) {
    CORD c = "(args";
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        c = CORD_all(c, " (arg ", arg->name ? CORD_quoted(arg->name) : "nil",
                     " ", type_ast_to_sexp(arg->type), " ", ast_to_sexp(arg->value), ")");
    }
    return CORD_cat(c, ")");
}

CORD arg_list_to_sexp(arg_ast_t *args) {
    CORD c = CORD_EMPTY;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        assert(arg->value && !arg->type);
        if (arg->name)
            c = CORD_all(c, " :", arg->name);
        c = CORD_all(c, " ", ast_to_sexp(arg->value));
    }
    return c;
}

CORD when_clauses_to_sexp(when_clause_t *clauses) {
    CORD c = CORD_EMPTY;
    for (; clauses; clauses = clauses->next) {
        c = CORD_all(c, " (case ", ast_to_sexp(clauses->pattern), " ", ast_to_sexp(clauses->body), ")");
    }
    return c;
}

CORD tags_to_sexp(tag_ast_t *tags) {
    CORD c = CORD_EMPTY;
    for (; tags; tags = tags->next) {
        c = CORD_all(c, "(tag \"", tags->name, "\" ", arg_defs_to_sexp(tags->fields), ")");
    }
    return c;
}

CORD type_ast_to_sexp(type_ast_t *t)
{
    if (!t) return "nil";

    switch (t->tag) {
#define T(type, ...) case type: { __typeof(t->__data.type) data = t->__data.type; (void)data; return CORD_all(__VA_ARGS__); }
    T(UnknownTypeAST, "(UnknownType)")
    T(VarTypeAST, "(VarType \"", data.name, "\")")
    T(PointerTypeAST, "(PointerType \"", data.is_stack ? "stack" : "heap", "\" ", type_ast_to_sexp(data.pointed), ")")
    T(ListTypeAST, "(ListType ", type_ast_to_sexp(data.item), ")")
    T(SetTypeAST, "(SetType ", type_ast_to_sexp(data.item), ")")
    T(TableTypeAST, "(TableType ", type_ast_to_sexp(data.key), " ", type_ast_to_sexp(data.value), ")")
    T(FunctionTypeAST, "(FunctionType ", arg_defs_to_sexp(data.args), " ", type_ast_to_sexp(data.ret), ")")
    T(OptionalTypeAST, "(OptionalType ", type_ast_to_sexp(data.type), ")")
#undef T
    default: return CORD_EMPTY;
    }
}

CORD optional_sexp(const char *name, ast_t *ast)
{
    return ast ? CORD_all(" :", name, " ", ast_to_sexp(ast)) : CORD_EMPTY;
}

CORD optional_type_sexp(const char *name, type_ast_t *ast)
{
    return ast ? CORD_all(" :", name, " ", type_ast_to_sexp(ast)) : CORD_EMPTY;
}

CORD ast_to_sexp(ast_t *ast)
{
    if (!ast) return "nil";

    switch (ast->tag) {
#define T(type, ...) case type: { __typeof(ast->__data.type) data = ast->__data.type; (void)data; return CORD_all(__VA_ARGS__); }
    T(Unknown,  "(Unknown)")
    T(None, "(None)")
    T(Bool, "(Bool ", data.b ? "yes" : "no", ")")
    T(Var, "(Var ", CORD_quoted(data.name), ")")
    T(Int, "(Int ", CORD_quoted(ast_source(ast)), ")")
    T(Dec, "(Dec ", CORD_quoted(ast_source(ast)), ")")
    T(Num, "(Num ", CORD_quoted(ast_source(ast)), ")")
    T(TextLiteral, CORD_quoted(data.cord))
    T(TextJoin, "(Text", data.lang ? CORD_all(" :lang ", CORD_quoted(data.lang)) : CORD_EMPTY, ast_list_to_sexp(data.children), ")")
    T(Path, "(Path ", CORD_quoted(data.path), ")")
    T(Declare, "(Declare ", ast_to_sexp(data.var), " ", type_ast_to_sexp(data.type), " ", ast_to_sexp(data.value), ")")
    T(Assign, "(Assign (targets ", ast_list_to_sexp(data.targets), ") (values ", ast_list_to_sexp(data.values), "))")
#define BINOP(name) T(name, "(" #name " ", ast_to_sexp(data.lhs), " ", ast_to_sexp(data.rhs), ")")
    BINOP(Power) BINOP(PowerUpdate) BINOP(Multiply) BINOP(MultiplyUpdate) BINOP(Divide) BINOP(DivideUpdate) BINOP(Mod) BINOP(ModUpdate)
    BINOP(Mod1) BINOP(Mod1Update) BINOP(Plus) BINOP(PlusUpdate) BINOP(Minus) BINOP(MinusUpdate) BINOP(Concat) BINOP(ConcatUpdate)
    BINOP(LeftShift) BINOP(LeftShiftUpdate) BINOP(RightShift) BINOP(RightShiftUpdate) BINOP(UnsignedLeftShift) BINOP(UnsignedLeftShiftUpdate)
    BINOP(UnsignedRightShift) BINOP(UnsignedRightShiftUpdate) BINOP(And) BINOP(AndUpdate) BINOP(Or) BINOP(OrUpdate)
    BINOP(Xor) BINOP(XorUpdate) BINOP(Compare)
    BINOP(Equals) BINOP(NotEquals) BINOP(LessThan) BINOP(LessThanOrEquals) BINOP(GreaterThan) BINOP(GreaterThanOrEquals)
#undef BINOP
    T(Negative, "(Negative ", ast_to_sexp(data.value), ")")
    T(Not, "(Not ", ast_to_sexp(data.value), ")")
    T(HeapAllocate, "(HeapAllocate ", ast_to_sexp(data.value), ")")
    T(StackReference, "(StackReference ", ast_to_sexp(data.value), ")")
    T(Min, "(Min ", ast_to_sexp(data.lhs), " ", ast_to_sexp(data.rhs), optional_sexp("key", data.key), ")")
    T(Max, "(Max ", ast_to_sexp(data.lhs), " ", ast_to_sexp(data.rhs), optional_sexp("key", data.key), ")")
    T(List, "(List", ast_list_to_sexp(data.items), ")")
    T(Set, "(Set", ast_list_to_sexp(data.items), ")")
    T(Table, "(Table", optional_sexp("default", data.default_value), optional_sexp("fallback", data.fallback),
      ast_list_to_sexp(data.entries), ")")
    T(TableEntry, "(TableEntry ", ast_to_sexp(data.key), " ", ast_to_sexp(data.value), ")")
    T(Comprehension, "(Comprehension ", ast_to_sexp(data.expr), " (vars", ast_list_to_sexp(data.vars), ") ",
      ast_to_sexp(data.iter), " ", optional_sexp("filter", data.filter), ")")
    T(FunctionDef, "(FunctionDef ", ast_to_sexp(data.name), " ", arg_defs_to_sexp(data.args),
      optional_type_sexp("return", data.ret_type), " ", ast_to_sexp(data.body), ")")
    T(ConvertDef, "(ConvertDef ", arg_defs_to_sexp(data.args), " ", type_ast_to_sexp(data.ret_type), " ", ast_to_sexp(data.body), ")")
    T(Lambda, "(Lambda ", arg_defs_to_sexp(data.args), optional_type_sexp("return", data.ret_type), " ", ast_to_sexp(data.body), ")")
    T(FunctionCall, "(FunctionCall ", ast_to_sexp(data.fn), arg_list_to_sexp(data.args), ")")
    T(MethodCall, "(MethodCall ", ast_to_sexp(data.self), " ", CORD_quoted(data.name), arg_list_to_sexp(data.args), ")")
    T(Block, "(Block", ast_list_to_sexp(data.statements), ")")
    T(For, "(For (vars", ast_list_to_sexp(data.vars), ") ", ast_to_sexp(data.iter), " ", ast_to_sexp(data.body),
      " ", ast_to_sexp(data.empty), ")")
    T(While, "(While ", ast_to_sexp(data.condition), " ", ast_to_sexp(data.body), ")")
    T(Repeat, "(Repeat ", ast_to_sexp(data.body), ")")
    T(If, "(If ", ast_to_sexp(data.condition), " ", ast_to_sexp(data.body), optional_sexp("else", data.else_body), ")")
    T(When, "(When ", ast_to_sexp(data.subject), when_clauses_to_sexp(data.clauses), optional_sexp("else", data.else_body), ")")
    T(Reduction, "(Reduction ", CORD_quoted(binop_method_name(data.op)), " ", ast_to_sexp(data.key), " ", ast_to_sexp(data.iter), ")")
    T(Skip, "(Skip ", CORD_quoted(data.target), ")")
    T(Stop, "(Stop ", CORD_quoted(data.target), ")")
    T(Pass, "(Pass)")
    T(Defer, "(Defer ", ast_to_sexp(data.body), ")")
    T(Return, "(Return ", ast_to_sexp(data.value), ")")
    T(Extern, "(Extern \"", data.name, "\" ", type_ast_to_sexp(data.type), ")")
    T(StructDef, "(StructDef \"", data.name, "\" ", arg_defs_to_sexp(data.fields), " ", ast_to_sexp(data.namespace), ")")
    T(EnumDef, "(EnumDef \"", data.name, "\" (tags ", tags_to_sexp(data.tags), ") ", ast_to_sexp(data.namespace), ")")
    T(LangDef, "(LangDef \"", data.name, "\" ", ast_to_sexp(data.namespace), ")")
    T(Index, "(Index ", ast_to_sexp(data.indexed), " ", ast_to_sexp(data.index), ")")
    T(FieldAccess, "(FieldAccess ", ast_to_sexp(data.fielded), " \"", data.field, "\")")
    T(Optional, "(Optional ", ast_to_sexp(data.value), ")")
    T(NonOptional, "(NonOptional ", ast_to_sexp(data.value), ")")
    T(DocTest, "(DocTest ", ast_to_sexp(data.expr), optional_sexp("expected", data.expected), ")")
    T(Assert, "(Assert ", ast_to_sexp(data.expr), " ", optional_sexp("message", data.message), ")")
    T(Use, "(Use ", optional_sexp("var", data.var), " ", CORD_quoted(data.path), ")")
    T(InlineCCode, "(InlineCCode ", ast_list_to_sexp(data.chunks), optional_type_sexp("type", data.type_ast), ")")
    T(Deserialize, "(Deserialize ", type_ast_to_sexp(data.type), " ", ast_to_sexp(data.value), ")")
    T(Extend, "(Extend \"", data.name, "\" ", ast_to_sexp(data.body), ")")
    default: errx(1, "S-expressions are not implemented for this AST"); 
#undef T
    }
}

const char *ast_to_sexp_str(ast_t *ast)
{
    return CORD_to_const_char_star(ast_to_sexp(ast));
}

const char *ast_source(ast_t *ast)
{
    if (!ast) return NULL;
    size_t len = (size_t)(ast->end - ast->start);
    char *source = GC_MALLOC_ATOMIC(len + 1);
    memcpy(source, ast->start, len);
    source[len] = '\0';
    return source;
}

PUREFUNC bool is_idempotent(ast_t *ast)
{
    switch (ast->tag) {
    case Int: case Bool: case Dec: case Num: case Var: case None: case TextLiteral: return true;
    case Index: {
        DeclareMatch(index, ast, Index);
        return is_idempotent(index->indexed) && index->index != NULL && is_idempotent(index->index);
    }
    case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        return is_idempotent(access->fielded);
    }
    default: return false;
    }
}

void _visit_topologically(ast_t *ast, Table_t definitions, Table_t *visited, Closure_t fn)
{
    void (*visit)(void*, ast_t*) = (void*)fn.fn;
    if (ast->tag == StructDef) {
        DeclareMatch(def, ast, StructDef);
        if (Table$str_get(*visited, def->name))
            return;

        Table$str_set(visited, def->name, (void*)_visit_topologically);
        for (arg_ast_t *field = def->fields; field; field = field->next) {
            if (field->type && field->type->tag == VarTypeAST) {
                const char *field_type_name = Match(field->type, VarTypeAST)->name;
                ast_t *dependency = Table$str_get(definitions, field_type_name);
                if (dependency) {
                    _visit_topologically(dependency, definitions, visited, fn);
                }
            }
                
        }
        visit(fn.userdata, ast);
    } else if (ast->tag == EnumDef) {
        DeclareMatch(def, ast, EnumDef);
        if (Table$str_get(*visited, def->name))
            return;

        Table$str_set(visited, def->name, (void*)_visit_topologically);
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                if (field->type && field->type->tag == VarTypeAST) {
                    const char *field_type_name = Match(field->type, VarTypeAST)->name;
                    ast_t *dependency = Table$str_get(definitions, field_type_name);
                    if (dependency) {
                        _visit_topologically(dependency, definitions, visited, fn);
                    }
                }
            }
        }
        visit(fn.userdata, ast);
    } else if (ast->tag == LangDef) {
        DeclareMatch(def, ast, LangDef);
        if (Table$str_get(*visited, def->name))
            return;
        visit(fn.userdata, ast);
    } else {
        visit(fn.userdata, ast);
    }
}

void visit_topologically(ast_list_t *asts, Closure_t fn)
{
    // Visit each top-level statement in topological order:
    // - 'use' statements first
    // - then typedefs
    //   - visiting typedefs' dependencies first
    // - then function/variable declarations

    Table_t definitions = {};
    for (ast_list_t *stmt = asts; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == StructDef) {
            DeclareMatch(def, stmt->ast, StructDef);
            Table$str_set(&definitions, def->name, stmt->ast);
        } else if (stmt->ast->tag == EnumDef) {
            DeclareMatch(def, stmt->ast, EnumDef);
            Table$str_set(&definitions, def->name, stmt->ast);
        } else if (stmt->ast->tag == LangDef) {
            DeclareMatch(def, stmt->ast, LangDef);
            Table$str_set(&definitions, def->name, stmt->ast);
        }
    }

    void (*visit)(void*, ast_t*) = (void*)fn.fn;
    Table_t visited = {};
    // First: 'use' statements in order:
    for (ast_list_t *stmt = asts; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use)
            visit(fn.userdata, stmt->ast);
    }
    // Then typedefs in topological order:
    for (ast_list_t *stmt = asts; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == StructDef || stmt->ast->tag == EnumDef || stmt->ast->tag == LangDef)
            _visit_topologically(stmt->ast, definitions, &visited, fn);
    }
    // Then everything else in order:
    for (ast_list_t *stmt = asts; stmt; stmt = stmt->next) {
        if (!(stmt->ast->tag == StructDef || stmt->ast->tag == EnumDef || stmt->ast->tag == LangDef
              || stmt->ast->tag == Use)) {
            visit(fn.userdata, stmt->ast);
        }
    }
}

CONSTFUNC bool is_binary_operation(ast_t *ast)
{
    switch (ast->tag) {
    case BINOP_CASES: return true;
    default: return false;
    }
}

CONSTFUNC bool is_update_assignment(ast_t *ast)
{
    switch (ast->tag) {
    case PowerUpdate: case MultiplyUpdate: case DivideUpdate: case ModUpdate: case Mod1Update:
    case PlusUpdate: case MinusUpdate: case ConcatUpdate: case LeftShiftUpdate: case UnsignedLeftShiftUpdate:
    case RightShiftUpdate: case UnsignedRightShiftUpdate: case AndUpdate: case OrUpdate: case XorUpdate:
        return true;
    default: return false;
    }
}

CONSTFUNC ast_e binop_tag(ast_e tag)
{
    switch (tag) {
    case PowerUpdate: return Power;
    case MultiplyUpdate: return Multiply;
    case DivideUpdate: return Divide;
    case ModUpdate: return Mod;
    case Mod1Update: return Mod1;
    case PlusUpdate: return Plus;
    case MinusUpdate: return Minus;
    case ConcatUpdate: return Concat;
    case LeftShiftUpdate: return LeftShift;
    case UnsignedLeftShiftUpdate: return UnsignedLeftShift;
    case RightShiftUpdate: return RightShift;
    case UnsignedRightShiftUpdate: return UnsignedRightShift;
    case AndUpdate: return And;
    case OrUpdate: return Or;
    case XorUpdate: return Xor;
    default: return Unknown;
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
