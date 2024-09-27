// Some basic operations defined on AST nodes, mainly converting to
// strings for debugging.
#include <gc/cord.h>
#include <printf.h>
#include <stdarg.h>

#include "ast.h"
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "cordhelpers.h"

static const char *OP_NAMES[] = {
    [BINOP_UNKNOWN]="unknown",
    [BINOP_POWER]="^", [BINOP_MULT]="*", [BINOP_DIVIDE]="/",
    [BINOP_MOD]="mod", [BINOP_MOD1]="mod1", [BINOP_PLUS]="+", [BINOP_MINUS]="minus",
    [BINOP_CONCAT]="++", [BINOP_LSHIFT]="<<", [BINOP_RSHIFT]=">>", [BINOP_MIN]="min",
    [BINOP_MAX]="max", [BINOP_EQ]="==", [BINOP_NE]="!=", [BINOP_LT]="<",
    [BINOP_LE]="<=", [BINOP_GT]=">", [BINOP_GE]=">=", [BINOP_CMP]="<>",
    [BINOP_AND]="and", [BINOP_OR]="or", [BINOP_XOR]="xor",
};

const char *binop_method_names[BINOP_XOR+1] = {
    [BINOP_POWER]="power", [BINOP_MULT]="times", [BINOP_DIVIDE]="divided_by",
    [BINOP_MOD]="modulo", [BINOP_MOD1]="modulo1", [BINOP_PLUS]="plus", [BINOP_MINUS]="minus",
    [BINOP_CONCAT]="concatenated_with", [BINOP_LSHIFT]="left_shifted", [BINOP_RSHIFT]="right_shifted",
    [BINOP_AND]="bit_and", [BINOP_OR]="bit_or", [BINOP_XOR]="bit_xor",
};

static CORD ast_list_to_xml(ast_list_t *asts);
static CORD arg_list_to_xml(arg_ast_t *args);
static CORD when_clauses_to_xml(when_clause_t *clauses);
static CORD tags_to_xml(tag_ast_t *tags);
static CORD xml_escape(CORD text);
static CORD optional_tagged(const char *tag, ast_t *ast);
static CORD optional_tagged_type(const char *tag, type_ast_t *ast);

CORD xml_escape(CORD text)
{
    text = CORD_replace(text, "&", "&amp;");
    text = CORD_replace(text, "<", "&lt;");
    text = CORD_replace(text, ">", "&gt;");
    return text;
}

CORD ast_list_to_xml(ast_list_t *asts)
{
    CORD c = CORD_EMPTY;
    for (; asts; asts = asts->next) {
        c = CORD_cat(c, ast_to_xml(asts->ast));
    }
    return c;
}

CORD arg_list_to_xml(arg_ast_t *args) {
    CORD c = "<args>";
    for (; args; args = args->next) {
        CORD arg_cord = args->name ? CORD_all("<arg name=\"", args->name, "\">") : "<arg>";
        if (args->type)
            arg_cord = CORD_all(arg_cord, "<type>", type_ast_to_xml(args->type), "</type>");
        if (args->value)
            arg_cord = CORD_all(arg_cord, "<value>", ast_to_xml(args->value), "</value>");
        c = CORD_all(c, arg_cord, "</arg>");
    }
    return CORD_cat(c, "</args>");
}

CORD when_clauses_to_xml(when_clause_t *clauses) {
    CORD c = CORD_EMPTY;
    for (; clauses; clauses = clauses->next) {
        c = CORD_all(c, "<case tag=\"", ast_to_xml(clauses->tag_name), "\">", ast_list_to_xml(clauses->args), ast_to_xml(clauses->body), "</case>");
    }
    return c;
}

CORD tags_to_xml(tag_ast_t *tags) {
    CORD c = CORD_EMPTY;
    for (; tags; tags = tags->next) {
        c = CORD_all(c, "<tag name=\"", tags->name, "\">", arg_list_to_xml(tags->fields), "</tag>");
    }
    return c;
}

CORD optional_tagged(const char *tag, ast_t *ast)
{
    return ast ? CORD_all("<", tag, ">", ast_to_xml(ast), "</", tag, ">") : CORD_EMPTY;
}

CORD optional_tagged_type(const char *tag, type_ast_t *ast)
{
    return ast ? CORD_all("<", tag, ">", type_ast_to_xml(ast), "</", tag, ">") : CORD_EMPTY;
}

CORD ast_to_xml(ast_t *ast)
{
    if (!ast) return CORD_EMPTY;

    switch (ast->tag) {
#define T(type, ...) case type: { auto data = ast->__data.type; (void)data; return CORD_asprintf(__VA_ARGS__); }
    T(Unknown,  "<Unknown>")
    T(Null, "<Null>%r</Null>", type_ast_to_xml(data.type))
    T(Bool, "<Bool value=\"%s\" />", data.b ? "yes" : "no")
    T(Var, "<Var>%s</Var>", data.name)
    T(Int, "<Int bits=\"%d\">%s</Int>", data.bits, data.str)
    T(Num, "<Num bits=\"%d\">%g</Num>", data.bits, data.n)
    T(TextLiteral, "%r", xml_escape(data.cord))
    T(TextJoin, "<Text%r>%r</Text>", data.lang ? CORD_all(" lang=\"", data.lang, "\"") : CORD_EMPTY, ast_list_to_xml(data.children))
    T(Declare, "<Declare var=\"%r\">%r</Declare>", ast_to_xml(data.var), ast_to_xml(data.value))
    T(Assign, "<Assign><targets>%r</targets><values>%r</values></Assign>", ast_list_to_xml(data.targets), ast_list_to_xml(data.values))
    T(BinaryOp, "<BinaryOp op=\"%r\">%r %r</BinaryOp>", xml_escape(OP_NAMES[data.op]), ast_to_xml(data.lhs), ast_to_xml(data.rhs))
    T(UpdateAssign, "<UpdateAssign op=\"%r\">%r %r</UpdateAssign>", xml_escape(OP_NAMES[data.op]), ast_to_xml(data.lhs), ast_to_xml(data.rhs))
    T(Negative, "<Negative>%r</Negative>", ast_to_xml(data.value))
    T(Not, "<Not>%r</Not>", ast_to_xml(data.value))
    T(HeapAllocate, "<HeapAllocate>%r</HeapAllocate>", ast_to_xml(data.value))
    T(StackReference, "<StackReference>%r</StackReference>", ast_to_xml(data.value))
    T(Min, "<Min>%r%r%r</Min>", ast_to_xml(data.lhs), ast_to_xml(data.rhs), optional_tagged("key", data.key))
    T(Max, "<Max>%r%r%r</Max>", ast_to_xml(data.lhs), ast_to_xml(data.rhs), optional_tagged("key", data.key))
    T(Array, "<Array>%r%r</Array>", optional_tagged_type("item-type", data.item_type), ast_list_to_xml(data.items))
    T(Set, "<Set>%r%r</Set>",
      optional_tagged_type("item-type", data.item_type),
      ast_list_to_xml(data.items))
    T(Table, "<Table>%r%r%r%r</Table>",
      optional_tagged_type("key-type", data.key_type), optional_tagged_type("value-type", data.value_type),
      ast_list_to_xml(data.entries), optional_tagged("fallback", data.fallback))
    T(TableEntry, "<TableEntry>%r%r</TableEntry>", ast_to_xml(data.key), ast_to_xml(data.value))
    T(Channel, "<Channel>%r%r</Channel>", type_ast_to_xml(data.item_type), optional_tagged("max-size", data.max_size))
    T(Comprehension, "<Comprehension>%r%r%r%r%r</Comprehension>", optional_tagged("expr", data.expr),
      ast_list_to_xml(data.vars), optional_tagged("iter", data.iter),
      optional_tagged("filter", data.filter))
    T(FunctionDef, "<FunctionDef name=\"%r\">%r%r<body>%r</body></FunctionDef>", ast_to_xml(data.name),
      arg_list_to_xml(data.args), optional_tagged_type("return-type", data.ret_type), ast_to_xml(data.body))
    T(Lambda, "<Lambda>%r<body>%r</body></Lambda>)", arg_list_to_xml(data.args), ast_to_xml(data.body))
    T(FunctionCall, "<FunctionCall><function>%r</function>%r</FunctionCall>", ast_to_xml(data.fn), arg_list_to_xml(data.args))
    T(MethodCall, "<MethodCall><self>%r</self><method>%s</method>%r</MethodCall>", ast_to_xml(data.self), data.name, arg_list_to_xml(data.args))
    T(Block, "<Block>%r</Block>", ast_list_to_xml(data.statements))
    T(For, "<For>%r%r%r%r%r</For>", ast_list_to_xml(data.vars), optional_tagged("iterable", data.iter),
      optional_tagged("body", data.body), optional_tagged("empty", data.empty))
    T(While, "<While>%r%r</While>", optional_tagged("condition", data.condition), optional_tagged("body", data.body))
    T(If, "<If>%r%r%r</If>", optional_tagged("condition", data.condition), optional_tagged("body", data.body), optional_tagged("else", data.else_body))
    T(When, "<When><subject>%r</subject>%r%r</When>", ast_to_xml(data.subject), when_clauses_to_xml(data.clauses), optional_tagged("else", data.else_body))
    T(Reduction, "<Reduction>%r%r%r</Reduction>", optional_tagged("iterable", data.iter),
      optional_tagged("combination", data.combination), optional_tagged("fallback", data.fallback))
    T(Skip, "<Skip>%r</Skip>", data.target)
    T(Stop, "<Stop>%r</Stop>", data.target)
    T(PrintStatement, "<PrintStatement>%r</PrintStatement>", ast_list_to_xml(data.to_print))
    T(Pass, "<Pass/>")
    T(Defer, "<Defer>%r<Defer/>", ast_to_xml(data.body))
    T(Return, "<Return>%r</Return>", ast_to_xml(data.value))
    T(Extern, "<Extern name=\"%s\">%r</Extern>", data.name, type_ast_to_xml(data.type))
    T(StructDef, "<StructDef name=\"%s\">%r<namespace>%r</namespace></StructDef>", data.name, arg_list_to_xml(data.fields), ast_to_xml(data.namespace))
    T(EnumDef, "<EnumDef name=\"%s\"><tags>%r</tags><namespace>%r</namespace></EnumDef>", data.name, tags_to_xml(data.tags), ast_to_xml(data.namespace))
    T(LangDef, "<LangDef name=\"%s\">%r</LangDef>", data.name, ast_to_xml(data.namespace))
    T(Index, "<Index>%r%r</Index>", optional_tagged("indexed", data.indexed), optional_tagged("index", data.index))
    T(FieldAccess, "<FieldAccess field=\"%s\">%r</FieldAccess>", data.field, ast_to_xml(data.fielded))
    T(Optional, "<Optional>%r</Optional>", ast_to_xml(data.value))
    T(NonOptional, "<NonOptional>%r</NonOptional>", ast_to_xml(data.value))
    T(DocTest, "<DocTest>%r<output>%r</output></DocTest>", optional_tagged("expression", data.expr), xml_escape(data.output))
    T(Use, "<Use>%r%r</Use>", optional_tagged("var", data.var), xml_escape(data.path))
    T(InlineCCode, "<InlineCode>%r</InlineCode>", xml_escape(data.code))
    default: return "???";
#undef T
    }
}

CORD type_ast_to_xml(type_ast_t *t)
{
    if (!t) return "NULL";

    switch (t->tag) {
#define T(type, ...) case type: { auto data = t->__data.type; (void)data; return CORD_asprintf(__VA_ARGS__); }
    T(UnknownTypeAST, "<UnknownType/>")
    T(VarTypeAST, "%s", data.name)
    T(PointerTypeAST, "<PointerType is_stack=\"%s\" is_readonly=\"%s\">%r</PointerType>",
      data.is_stack ? "yes" : "no", data.is_readonly ? "yes" : "no", type_ast_to_xml(data.pointed))
    T(ArrayTypeAST, "<ArrayType>%r</ArrayType>", type_ast_to_xml(data.item))
    T(SetTypeAST, "<TableType>%r</TableType>", type_ast_to_xml(data.item))
    T(ChannelTypeAST, "<ChannelType>%r</ChannelType>", type_ast_to_xml(data.item))
    T(TableTypeAST, "<TableType>%r %r</TableType>", type_ast_to_xml(data.key), type_ast_to_xml(data.value))
    T(FunctionTypeAST, "<FunctionType>%r %r</FunctionType>", arg_list_to_xml(data.args), type_ast_to_xml(data.ret))
    T(OptionalTypeAST, "<OptionalType>%r</OptionalType>", data.type)
#undef T
    default: return CORD_EMPTY;
    }
}

int printf_ast(FILE *stream, const struct printf_info *info, const void *const args[])
{
    ast_t *ast = *(ast_t**)(args[0]);
    if (ast) {
        if (info->alt)
            return fprintf(stream, "%.*s", (int)(ast->end - ast->start), ast->start);
        else
            return CORD_put(ast_to_xml(ast), stream);
    } else {
        return fputs("(null)", stream);
    }
}

PUREFUNC bool is_idempotent(ast_t *ast)
{
    switch (ast->tag) {
    case Int: case Bool: case Num: case Var: case Null: case TextLiteral: return true;
    case Index: {
        auto index = Match(ast, Index);
        return is_idempotent(index->indexed) && index->index != NULL && is_idempotent(index->index);
    }
    case FieldAccess: {
        auto access = Match(ast, FieldAccess);
        return is_idempotent(access->fielded);
    }
    default: return false;
    }
}

void _visit_topologically(ast_t *ast, Table_t definitions, Table_t *visited, Closure_t fn)
{
    void (*visit)(void*, ast_t*) = (void*)fn.fn;
    if (ast->tag == StructDef) {
        auto def = Match(ast, StructDef);
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
        auto def = Match(ast, EnumDef);
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
        auto def = Match(ast, LangDef);
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
            auto def = Match(stmt->ast, StructDef);
            Table$str_set(&definitions, def->name, stmt->ast);
        } else if (stmt->ast->tag == EnumDef) {
            auto def = Match(stmt->ast, EnumDef);
            Table$str_set(&definitions, def->name, stmt->ast);
        } else if (stmt->ast->tag == LangDef) {
            auto def = Match(stmt->ast, LangDef);
            Table$str_set(&definitions, def->name, stmt->ast);
        }
    }

    void (*visit)(void*, ast_t*) = (void*)fn.fn;
    Table_t visited = {};
    // First: 'use' statements in order:
    for (ast_list_t *stmt = asts; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use || (stmt->ast->tag == Declare && Match(stmt->ast, Declare)->value->tag == Use))
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
              || stmt->ast->tag == Use || (stmt->ast->tag == Declare && Match(stmt->ast, Declare)->value->tag == Use))) {
            visit(fn.userdata, stmt->ast);
        }
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
