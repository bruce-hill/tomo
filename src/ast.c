// Some basic operations defined on AST nodes, mainly converting to
// strings for debugging.

#include <stdarg.h>

#include "ast.h"
#include "stdlib/datatypes.h"
#include "stdlib/optionals.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"

const int op_tightness[NUM_AST_TAGS] = {
    [Power] = 9,
    [Multiply] = 8,
    [Divide] = 8,
    [Mod] = 8,
    [Mod1] = 8,
    [Plus] = 7,
    [Minus] = 7,
    [Concat] = 6,
    [LeftShift] = 5,
    [RightShift] = 5,
    [UnsignedLeftShift] = 5,
    [UnsignedRightShift] = 5,
    [Min] = 4,
    [Max] = 4,
    [Equals] = 3,
    [NotEquals] = 3,
    [LessThan] = 2,
    [LessThanOrEquals] = 2,
    [GreaterThan] = 2,
    [GreaterThanOrEquals] = 2,
    [Compare] = 2,
    [And] = 1,
    [Or] = 1,
    [Xor] = 1,
};

const binop_info_t binop_info[NUM_AST_TAGS] = {
    [Power] = {"power", "^"},
    [PowerUpdate] = {"power", "^="},
    [Multiply] = {"times", "*"},
    [MultiplyUpdate] = {"times", "*="},
    [Divide] = {"divided_by", "/"},
    [DivideUpdate] = {"divided_by", "/="},
    [Mod] = {"modulo", "mod"},
    [ModUpdate] = {"modulo", "mod="},
    [Mod1] = {"modulo1", "mod1"},
    [Mod1Update] = {"modulo1", "mod1="},
    [Plus] = {"plus", "+"},
    [PlusUpdate] = {"plus", "+="},
    [Minus] = {"minus", "-"},
    [MinusUpdate] = {"minus", "-="},
    [Concat] = {"concatenated_with", "++"},
    [ConcatUpdate] = {"concatenated_with", "++="},
    [LeftShift] = {"left_shifted", "<<"},
    [LeftShiftUpdate] = {"left_shifted", "<<="},
    [RightShift] = {"right_shifted", ">>"},
    [RightShiftUpdate] = {"right_shifted", ">>="},
    [UnsignedLeftShift] = {"unsigned_left_shifted", NULL},
    [UnsignedLeftShiftUpdate] = {"unsigned_left_shifted", NULL},
    [UnsignedRightShift] = {"unsigned_right_shifted", NULL},
    [UnsignedRightShiftUpdate] = {"unsigned_right_shifted", NULL},
    [And] = {"bit_and", "and"},
    [AndUpdate] = {"bit_and", "and="},
    [Or] = {"bit_or", "or"},
    [OrUpdate] = {"bit_or", "or="},
    [Xor] = {"bit_xor", "xor"},
    [XorUpdate] = {"bit_xor", "xor="},
    [Equals] = {NULL, "=="},
    [NotEquals] = {NULL, "!="},
    [LessThan] = {NULL, "<"},
    [LessThanOrEquals] = {NULL, "<="},
    [GreaterThan] = {NULL, ">"},
    [GreaterThanOrEquals] = {NULL, ">="},
    [Min] = {NULL, "_min_"},
    [Max] = {NULL, "_max_"},
};

static Text_t ast_list_to_sexp(ast_list_t *asts);
static Text_t arg_list_to_sexp(arg_ast_t *args);
static Text_t arg_defs_to_sexp(arg_ast_t *args);
static Text_t when_clauses_to_sexp(when_clause_t *clauses);
static Text_t tags_to_sexp(tag_ast_t *tags);
static Text_t optional_sexp(const char *tag, ast_t *ast);
static Text_t optional_type_sexp(const char *tag, type_ast_t *ast);

static Text_t quoted_text(const char *text) { return Text$quoted(Text$from_str(text), false, Text("\"")); }

Text_t ast_list_to_sexp(ast_list_t *asts) {
    Text_t c = EMPTY_TEXT;
    for (; asts; asts = asts->next) {
        c = Texts(c, " ", ast_to_sexp(asts->ast));
    }
    return c;
}

Text_t arg_defs_to_sexp(arg_ast_t *args) {
    Text_t c = Text("(args");
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        c = Texts(c, " (arg ", arg->name ? quoted_text(arg->name) : Text("nil"), " ", type_ast_to_sexp(arg->type), " ",
                  ast_to_sexp(arg->value), ")");
    }
    return Texts(c, ")");
}

Text_t arg_list_to_sexp(arg_ast_t *args) {
    Text_t c = EMPTY_TEXT;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        assert(arg->value && !arg->type);
        if (arg->name) c = Texts(c, " :", arg->name);
        c = Texts(c, " ", ast_to_sexp(arg->value));
    }
    return c;
}

Text_t when_clauses_to_sexp(when_clause_t *clauses) {
    Text_t c = EMPTY_TEXT;
    for (; clauses; clauses = clauses->next) {
        c = Texts(c, " (case ", ast_to_sexp(clauses->pattern), " ", ast_to_sexp(clauses->body), ")");
    }
    return c;
}

Text_t tags_to_sexp(tag_ast_t *tags) {
    Text_t c = EMPTY_TEXT;
    for (; tags; tags = tags->next) {
        c = Texts(c, "(tag \"", tags->name, "\" ", arg_defs_to_sexp(tags->fields), ")");
    }
    return c;
}

Text_t type_ast_to_sexp(type_ast_t *t) {
    if (!t) return Text("nil");

    switch (t->tag) {
#define T(type, ...)                                                                                                   \
    case type: {                                                                                                       \
        __typeof(t->__data.type) data = t->__data.type;                                                                \
        (void)data;                                                                                                    \
        return Texts(__VA_ARGS__);                                                                                     \
    }
        T(UnknownTypeAST, "(UnknownType)");
        T(VarTypeAST, "(VarType \"", data.name, "\")");
        T(PointerTypeAST, "(PointerType \"", data.is_stack ? "stack" : "heap", "\" ", type_ast_to_sexp(data.pointed),
          ")");
        T(ListTypeAST, "(ListType ", type_ast_to_sexp(data.item), ")");
        T(SetTypeAST, "(SetType ", type_ast_to_sexp(data.item), ")");
        T(TableTypeAST, "(TableType ", type_ast_to_sexp(data.key), " ", type_ast_to_sexp(data.value), ")");
        T(FunctionTypeAST, "(FunctionType ", arg_defs_to_sexp(data.args), " ", type_ast_to_sexp(data.ret), ")");
        T(OptionalTypeAST, "(OptionalType ", type_ast_to_sexp(data.type), ")");
        T(EnumTypeAST, "(EnumType ", data.name, " ", tags_to_sexp(data.tags), ")");
#undef T
    default: return EMPTY_TEXT;
    }
}

Text_t optional_sexp(const char *name, ast_t *ast) {
    return ast ? Texts(" :", name, " ", ast_to_sexp(ast)) : EMPTY_TEXT;
}

Text_t optional_type_sexp(const char *name, type_ast_t *ast) {
    return ast ? Texts(" :", name, " ", type_ast_to_sexp(ast)) : EMPTY_TEXT;
}

Text_t ast_to_sexp(ast_t *ast) {
    if (!ast) return Text("nil");

    switch (ast->tag) {
#define T(type, ...)                                                                                                   \
    case type: {                                                                                                       \
        __typeof(ast->__data.type) data = ast->__data.type;                                                            \
        (void)data;                                                                                                    \
        return Texts(__VA_ARGS__);                                                                                     \
    }
        T(Unknown, "(Unknown)");
        T(None, "(None)");
        T(Bool, "(Bool ", data.b ? "yes" : "no", ")");
        T(Var, "(Var ", quoted_text(data.name), ")");
        T(Int, "(Int ", Text$quoted(ast_source(ast), false, Text("\"")), ")");
        T(Num, "(Num ", Text$quoted(ast_source(ast), false, Text("\"")), ")");
        T(TextLiteral, Text$quoted(data.text, false, Text("\"")));
        T(TextJoin, "(Text", data.lang ? Texts(" :lang ", quoted_text(data.lang)) : EMPTY_TEXT,
          ast_list_to_sexp(data.children), ")");
        T(Path, "(Path ", quoted_text(data.path), ")");
        T(Declare, "(Declare ", ast_to_sexp(data.var), " ", type_ast_to_sexp(data.type), " ", ast_to_sexp(data.value),
          ")");
        T(Assign, "(Assign (targets ", ast_list_to_sexp(data.targets), ") (values ", ast_list_to_sexp(data.values),
          "))");
#define BINOP(name) T(name, "(" #name " ", ast_to_sexp(data.lhs), " ", ast_to_sexp(data.rhs), ")")
        BINOP(Power);
        BINOP(PowerUpdate);
        BINOP(Multiply);
        BINOP(MultiplyUpdate);
        BINOP(Divide);
        BINOP(DivideUpdate);
        BINOP(Mod);
        BINOP(ModUpdate);
        BINOP(Mod1);
        BINOP(Mod1Update);
        BINOP(Plus);
        BINOP(PlusUpdate);
        BINOP(Minus);
        BINOP(MinusUpdate);
        BINOP(Concat);
        BINOP(ConcatUpdate);
        BINOP(LeftShift);
        BINOP(LeftShiftUpdate);
        BINOP(RightShift);
        BINOP(RightShiftUpdate);
        BINOP(UnsignedLeftShift);
        BINOP(UnsignedLeftShiftUpdate);
        BINOP(UnsignedRightShift);
        BINOP(UnsignedRightShiftUpdate);
        BINOP(And);
        BINOP(AndUpdate);
        BINOP(Or);
        BINOP(OrUpdate);
        BINOP(Xor);
        BINOP(XorUpdate);
        BINOP(Compare);
        BINOP(Equals);
        BINOP(NotEquals);
        BINOP(LessThan);
        BINOP(LessThanOrEquals);
        BINOP(GreaterThan);
        BINOP(GreaterThanOrEquals);
#undef BINOP
        T(Negative, "(Negative ", ast_to_sexp(data.value), ")");
        T(Not, "(Not ", ast_to_sexp(data.value), ")");
        T(HeapAllocate, "(HeapAllocate ", ast_to_sexp(data.value), ")");
        T(StackReference, "(StackReference ", ast_to_sexp(data.value), ")");
        T(Min, "(Min ", ast_to_sexp(data.lhs), " ", ast_to_sexp(data.rhs), optional_sexp("key", data.key), ")");
        T(Max, "(Max ", ast_to_sexp(data.lhs), " ", ast_to_sexp(data.rhs), optional_sexp("key", data.key), ")");
        T(List, "(List", ast_list_to_sexp(data.items), ")");
        T(Set, "(Set", ast_list_to_sexp(data.items), ")");
        T(Table, "(Table", optional_sexp("default", data.default_value), optional_sexp("fallback", data.fallback),
          ast_list_to_sexp(data.entries), ")");
        T(TableEntry, "(TableEntry ", ast_to_sexp(data.key), " ", ast_to_sexp(data.value), ")");
        T(Comprehension, "(Comprehension ", ast_to_sexp(data.expr), " (vars", ast_list_to_sexp(data.vars), ") ",
          ast_to_sexp(data.iter), " ", optional_sexp("filter", data.filter), ")");
        T(FunctionDef, "(FunctionDef ", ast_to_sexp(data.name), " ", arg_defs_to_sexp(data.args),
          optional_type_sexp("return", data.ret_type), " ", ast_to_sexp(data.body), ")");
        T(ConvertDef, "(ConvertDef ", arg_defs_to_sexp(data.args), " ", type_ast_to_sexp(data.ret_type), " ",
          ast_to_sexp(data.body), ")");
        T(Lambda, "(Lambda ", arg_defs_to_sexp(data.args), optional_type_sexp("return", data.ret_type), " ",
          ast_to_sexp(data.body), ")");
        T(FunctionCall, "(FunctionCall ", ast_to_sexp(data.fn), arg_list_to_sexp(data.args), ")");
        T(MethodCall, "(MethodCall ", ast_to_sexp(data.self), " ", quoted_text(data.name), arg_list_to_sexp(data.args),
          ")")
        T(Block, "(Block", ast_list_to_sexp(data.statements), ")");
        T(For, "(For (vars", ast_list_to_sexp(data.vars), ") ", ast_to_sexp(data.iter), " ", ast_to_sexp(data.body),
          " ", ast_to_sexp(data.empty), ")");
        T(While, "(While ", ast_to_sexp(data.condition), " ", ast_to_sexp(data.body), ")");
        T(Repeat, "(Repeat ", ast_to_sexp(data.body), ")");
        T(If, "(If ", ast_to_sexp(data.condition), " ", ast_to_sexp(data.body), optional_sexp("else", data.else_body),
          ")");
        T(When, "(When ", ast_to_sexp(data.subject), when_clauses_to_sexp(data.clauses),
          optional_sexp("else", data.else_body), ")");
        T(Reduction, "(Reduction ", quoted_text(binop_info[data.op].operator), " ", ast_to_sexp(data.key), " ",
          ast_to_sexp(data.iter), ")");
        T(Skip, "(Skip ", quoted_text(data.target), ")");
        T(Stop, "(Stop ", quoted_text(data.target), ")");
        T(Pass, "(Pass)");
        T(Defer, "(Defer ", ast_to_sexp(data.body), ")");
        T(Return, "(Return ", ast_to_sexp(data.value), ")");
        T(Extern, "(Extern \"", data.name, "\" ", type_ast_to_sexp(data.type), ")");
        T(StructDef, "(StructDef \"", data.name, "\" ", arg_defs_to_sexp(data.fields), " ", ast_to_sexp(data.namespace),
          ")");
        T(EnumDef, "(EnumDef \"", data.name, "\" (tags ", tags_to_sexp(data.tags), ") ", ast_to_sexp(data.namespace),
          ")");
        T(LangDef, "(LangDef \"", data.name, "\" ", ast_to_sexp(data.namespace), ")");
        T(Index, "(Index ", ast_to_sexp(data.indexed), " ", ast_to_sexp(data.index), ")");
        T(FieldAccess, "(FieldAccess ", ast_to_sexp(data.fielded), " \"", data.field, "\")");
        T(Optional, "(Optional ", ast_to_sexp(data.value), ")");
        T(NonOptional, "(NonOptional ", ast_to_sexp(data.value), ")");
        T(DocTest, "(DocTest ", ast_to_sexp(data.expr), optional_sexp("expected", data.expected), ")");
        T(Assert, "(Assert ", ast_to_sexp(data.expr), " ", optional_sexp("message", data.message), ")");
        T(Use, "(Use ", optional_sexp("var", data.var), " ", quoted_text(data.path), ")");
        T(InlineCCode, "(InlineCCode ", ast_list_to_sexp(data.chunks), optional_type_sexp("type", data.type_ast), ")");
        T(Deserialize, "(Deserialize ", type_ast_to_sexp(data.type), " ", ast_to_sexp(data.value), ")");
        T(Extend, "(Extend \"", data.name, "\" ", ast_to_sexp(data.body), ")");
    default: errx(1, "S-expressions are not implemented for this AST");
#undef T
    }
}

const char *ast_to_sexp_str(ast_t *ast) { return Text$as_c_string(ast_to_sexp(ast)); }

OptionalText_t ast_source(ast_t *ast) {
    if (ast == NULL || ast->start == NULL || ast->end == NULL) return NONE_TEXT;
    return Text$from_strn(ast->start, (size_t)(ast->end - ast->start));
}

PUREFUNC bool is_idempotent(ast_t *ast) {
    switch (ast->tag) {
    case Int:
    case Bool:
    case Num:
    case Var:
    case None:
    case TextLiteral: return true;
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

void _visit_topologically(ast_t *ast, Table_t definitions, Table_t *visited, Closure_t fn) {
    void (*visit)(void *, ast_t *) = (void *)fn.fn;
    if (ast->tag == StructDef) {
        DeclareMatch(def, ast, StructDef);
        if (Table$str_get(*visited, def->name)) return;

        Table$str_set(visited, def->name, (void *)_visit_topologically);
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
        if (Table$str_get(*visited, def->name)) return;

        Table$str_set(visited, def->name, (void *)_visit_topologically);
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
        if (Table$str_get(*visited, def->name)) return;
        visit(fn.userdata, ast);
    } else {
        visit(fn.userdata, ast);
    }
}

void visit_topologically(ast_list_t *asts, Closure_t fn) {
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

    void (*visit)(void *, ast_t *) = (void *)fn.fn;
    Table_t visited = {};
    // First: 'use' statements in order:
    for (ast_list_t *stmt = asts; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) visit(fn.userdata, stmt->ast);
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

CONSTFUNC bool is_binary_operation(ast_t *ast) {
    switch (ast->tag) {
    case Min:
    case Max:
    case BINOP_CASES: return true;
    default: return false;
    }
}

CONSTFUNC bool is_update_assignment(ast_t *ast) {
    switch (ast->tag) {
    case PowerUpdate:
    case MultiplyUpdate:
    case DivideUpdate:
    case ModUpdate:
    case Mod1Update:
    case PlusUpdate:
    case MinusUpdate:
    case ConcatUpdate:
    case LeftShiftUpdate:
    case UnsignedLeftShiftUpdate:
    case RightShiftUpdate:
    case UnsignedRightShiftUpdate:
    case AndUpdate:
    case OrUpdate:
    case XorUpdate: return true;
    default: return false;
    }
}

CONSTFUNC ast_e binop_tag(ast_e tag) {
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
