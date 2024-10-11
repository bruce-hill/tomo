// Some basic operations defined on AST nodes, mainly converting to
// strings for debugging.
#include <printf.h>
#include <stdarg.h>

#include "ast.h"
#include "stdlib/arrays.h"
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/patterns.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"

#define ast_to_xml_ptr(ast) stack(ast_to_xml(ast))
#define type_ast_to_xml_ptr(ast) stack(type_ast_to_xml(ast))

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

static Text_t _ast_list_to_xml(ast_list_t *asts);
#define ast_list_to_xml(ast) stack(_ast_list_to_xml(ast))
static Text_t _arg_list_to_xml(arg_ast_t *args);
#define arg_list_to_xml(ast) stack(_arg_list_to_xml(ast))
static Text_t _when_clauses_to_xml(when_clause_t *clauses);
#define when_clauses_to_xml(...) stack(_when_clauses_to_xml(__VA_ARGS__))
static Text_t _tags_to_xml(tag_ast_t *tags);
#define tags_to_xml(...) stack(_tags_to_xml(__VA_ARGS__))
static Text_t _xml_escape(Text_t text);
#define xml_escape(...) stack(_xml_escape(__VA_ARGS__))
static Text_t _optional_tagged(const char *tag, ast_t *ast);
#define optional_tagged(...) stack(_optional_tagged(__VA_ARGS__))
static Text_t _optional_tagged_type(const char *tag, type_ast_t *ast);
#define optional_tagged_type(...) stack(_optional_tagged_type(__VA_ARGS__))

Text_t _xml_escape(Text_t text)
{
    typedef struct {Text_t k,v;} replacement_t;
    Array_t replacements = TypedArray(
        replacement_t,
        {Text("&"), Text("&amp;")},
        {Text("<"), Text("&lt;")},
        {Text(">"), Text("&gt;")});
    return Text$replace_all(text, Table$from_entries(replacements, Table$info(&Pattern$info, &Text$info)), Text(""), false);
}

Text_t _ast_list_to_xml(ast_list_t *asts)
{
    Text_t text = Text("");
    for (; asts; asts = asts->next) {
        text = Texts(text, ast_to_xml(asts->ast));
    }
    return text;
}

Text_t _arg_list_to_xml(arg_ast_t *args) {
    Text_t text = Text("<args>");
    for (; args; args = args->next) {
        Text_t arg_text = args->name ? Text$format("<arg name=\"%s\">", args->name) : Text("<arg>");
        if (args->type)
            arg_text = Texts(arg_text, Text("<type>"), type_ast_to_xml(args->type), Text("</type>"));
        if (args->value)
            arg_text = Texts(text, Text("<value>"), ast_to_xml(args->value), Text("</value>"));
        text = Texts(text, arg_text, Text("</arg>"));
    }
    return Texts(text, Text("</args>"));
}

Text_t _when_clauses_to_xml(when_clause_t *clauses) {
    Text_t text = Text("");
    for (; clauses; clauses = clauses->next) {
        text = Texts(text, Text("<case tag=\""), ast_to_xml(clauses->tag_name), Text("\">"),
                     _ast_list_to_xml(clauses->args), ast_to_xml(clauses->body), Text("</case>"));
    }
    return text;
}

Text_t _tags_to_xml(tag_ast_t *tags) {
    Text_t text = Text("");
    for (; tags; tags = tags->next) {
        text = Texts(text, Text$format("<tag name=\"%s\">%k</tag>", tags->name, arg_list_to_xml(tags->fields)));
    }
    return text;
}

Text_t _optional_tagged(const char *tag, ast_t *ast)
{
    return ast ? Text$format("<%s>%k</%s>", tag, ast_to_xml_ptr(ast), tag) : Text("");
}

Text_t _optional_tagged_type(const char *tag, type_ast_t *ast)
{
    return ast ? Text$format("<%s>%k</%s>", tag, stack(type_ast_to_xml(ast)), tag) : Text("");
}

Text_t ast_to_xml(ast_t *ast)
{
    if (!ast) return Text("");

    switch (ast->tag) {
#define T(type, ...) case type: { auto data = ast->__data.type; (void)data; return Text$format(__VA_ARGS__); }
    T(Unknown,  "<Unknown>")
    T(Null, "<Null>%k</Null>", stack(type_ast_to_xml(data.type)))
    T(Bool, "<Bool value=\"%s\" />", data.b ? "yes" : "no")
    T(Var, "<Var>%s</Var>", data.name)
    T(Int, "<Int bits=\"%d\">%s</Int>", data.bits, data.str)
    T(Num, "<Num bits=\"%d\">%g</Num>", data.bits, data.n)
    T(TextLiteral, "%k", xml_escape(data.text))
    T(TextJoin, "<Text%k>%k</Text>", stack(data.lang ? Text$format(" lang=\"%s\"", data.lang) : Text("")), ast_list_to_xml(data.children))
    T(Declare, "<Declare var=\"%k\">%k</Declare>", ast_to_xml_ptr(data.var), ast_to_xml_ptr(data.value))
    T(Assign, "<Assign><targets>%k</targets><values>%k</values></Assign>", ast_list_to_xml(data.targets), ast_list_to_xml(data.values))
    T(BinaryOp, "<BinaryOp op=\"%k\">%k %k</BinaryOp>", xml_escape(Text$from_str(OP_NAMES[data.op])),
      ast_to_xml_ptr(data.lhs), ast_to_xml_ptr(data.rhs))
    T(UpdateAssign, "<UpdateAssign op=\"%k\">%k %k</UpdateAssign>", xml_escape(Text$from_str(OP_NAMES[data.op])),
      ast_to_xml_ptr(data.lhs), ast_to_xml_ptr(data.rhs))
    T(Negative, "<Negative>%k</Negative>", ast_to_xml_ptr(data.value))
    T(Not, "<Not>%k</Not>", ast_to_xml_ptr(data.value))
    T(HeapAllocate, "<HeapAllocate>%k</HeapAllocate>", ast_to_xml_ptr(data.value))
    T(StackReference, "<StackReference>%k</StackReference>", ast_to_xml_ptr(data.value))
    T(Min, "<Min>%k%k%k</Min>", ast_to_xml_ptr(data.lhs), ast_to_xml_ptr(data.rhs), optional_tagged("key", data.key))
    T(Max, "<Max>%k%k%k</Max>", ast_to_xml_ptr(data.lhs), ast_to_xml_ptr(data.rhs), optional_tagged("key", data.key))
    T(Array, "<Array>%k%k</Array>", optional_tagged_type("item-type", data.item_type), ast_list_to_xml(data.items))
    T(Set, "<Set>%k%k</Set>",
      optional_tagged_type("item-type", data.item_type), ast_list_to_xml(data.items))
    T(Table, "<Table>%k%k%k%k</Table>",
      optional_tagged_type("key-type", data.key_type), optional_tagged_type("value-type", data.value_type),
      ast_list_to_xml(data.entries), optional_tagged("fallback", data.fallback))
    T(TableEntry, "<TableEntry>%k%k</TableEntry>", ast_to_xml_ptr(data.key), ast_to_xml_ptr(data.value))
    T(Channel, "<Channel>%k%k</Channel>", type_ast_to_xml_ptr(data.item_type), optional_tagged("max-size", data.max_size))
    T(Comprehension, "<Comprehension>%k%k%k%k%k</Comprehension>", optional_tagged("expr", data.expr),
      ast_list_to_xml(data.vars), optional_tagged("iter", data.iter),
      optional_tagged("filter", data.filter))
    T(FunctionDef, "<FunctionDef name=\"%k\">%k%k<body>%k</body></FunctionDef>", ast_to_xml_ptr(data.name),
      arg_list_to_xml(data.args), optional_tagged_type("return-type", data.ret_type), ast_to_xml_ptr(data.body))
    T(Lambda, "<Lambda>%k%k<body>%k</body></Lambda>)", arg_list_to_xml(data.args),
      optional_tagged_type("return-type", data.ret_type), ast_to_xml_ptr(data.body))
    T(FunctionCall, "<FunctionCall><function>%k</function>%k</FunctionCall>", ast_to_xml_ptr(data.fn), arg_list_to_xml(data.args))
    T(MethodCall, "<MethodCall><self>%k</self><method>%s</method>%k</MethodCall>", ast_to_xml_ptr(data.self), data.name, arg_list_to_xml(data.args))
    T(Block, "<Block>%k</Block>", ast_list_to_xml(data.statements))
    T(For, "<For>%k%k%k%k%k</For>", ast_list_to_xml(data.vars), optional_tagged("iterable", data.iter),
      optional_tagged("body", data.body), optional_tagged("empty", data.empty))
    T(While, "<While>%k%k</While>", optional_tagged("condition", data.condition), optional_tagged("body", data.body))
    T(If, "<If>%k%k%k</If>", optional_tagged("condition", data.condition), optional_tagged("body", data.body), optional_tagged("else", data.else_body))
    T(When, "<When><subject>%k</subject>%k%k</When>", ast_to_xml_ptr(data.subject), when_clauses_to_xml(data.clauses), optional_tagged("else", data.else_body))
    T(Reduction, "<Reduction>%k%k%k</Reduction>", optional_tagged("iterable", data.iter),
      optional_tagged("combination", data.combination), optional_tagged("fallback", data.fallback))
    T(Skip, "<Skip>%s</Skip>", data.target)
    T(Stop, "<Stop>%s</Stop>", data.target)
    T(PrintStatement, "<PrintStatement>%k</PrintStatement>", ast_list_to_xml(data.to_print))
    T(Pass, "<Pass/>")
    T(Defer, "<Defer>%k<Defer/>", ast_to_xml_ptr(data.body))
    T(Return, "<Return>%k</Return>", ast_to_xml_ptr(data.value))
    T(Extern, "<Extern name=\"%s\">%k</Extern>", data.name, type_ast_to_xml_ptr(data.type))
    T(StructDef, "<StructDef name=\"%s\">%k<namespace>%k</namespace></StructDef>", data.name, arg_list_to_xml(data.fields), ast_to_xml_ptr(data.namespace))
    T(EnumDef, "<EnumDef name=\"%s\"><tags>%k</tags><namespace>%k</namespace></EnumDef>", data.name, tags_to_xml(data.tags), ast_to_xml_ptr(data.namespace))
    T(LangDef, "<LangDef name=\"%s\">%k</LangDef>", data.name, ast_to_xml_ptr(data.namespace))
    T(Index, "<Index>%k%k</Index>", optional_tagged("indexed", data.indexed), optional_tagged("index", data.index))
    T(FieldAccess, "<FieldAccess field=\"%s\">%k</FieldAccess>", data.field, ast_to_xml_ptr(data.fielded))
    T(Optional, "<Optional>%k</Optional>", ast_to_xml_ptr(data.value))
    T(NonOptional, "<NonOptional>%k</NonOptional>", ast_to_xml_ptr(data.value))
    T(DocTest, "<DocTest>%k<output>%k</output></DocTest>", optional_tagged("expression", data.expr), xml_escape(Text$from_str(data.output)))
    T(Use, "<Use>%k%k</Use>", optional_tagged("var", data.var), xml_escape(Text$from_str(data.path)))
    T(InlineCCode, "<InlineCode>%k</InlineCode>", xml_escape(data.code))
    default: return Text("???");
#undef T
    }
}

Text_t type_ast_to_xml(type_ast_t *t)
{
    if (!t) return Text("NULL");

    switch (t->tag) {
#define T(type, ...) case type: { auto data = t->__data.type; (void)data; return Text$format(__VA_ARGS__); }
    T(UnknownTypeAST, "<UnknownType/>")
    T(VarTypeAST, "%s", data.name)
    T(PointerTypeAST, "<PointerType is_stack=\"%s\">%k</PointerType>",
      data.is_stack ? "yes" : "no", type_ast_to_xml_ptr(data.pointed))
    T(ArrayTypeAST, "<ArrayType>%k</ArrayType>", type_ast_to_xml_ptr(data.item))
    T(SetTypeAST, "<TableType>%k</TableType>", type_ast_to_xml_ptr(data.item))
    T(ChannelTypeAST, "<ChannelType>%k</ChannelType>", type_ast_to_xml_ptr(data.item))
    T(TableTypeAST, "<TableType>%k %k</TableType>", type_ast_to_xml_ptr(data.key), type_ast_to_xml_ptr(data.value))
    T(FunctionTypeAST, "<FunctionType>%k %k</FunctionType>", arg_list_to_xml(data.args), type_ast_to_xml_ptr(data.ret))
    T(OptionalTypeAST, "<OptionalType>%k</OptionalType>", type_ast_to_xml_ptr(data.type))
#undef T
    default: return Text("");
    }
}

int printf_ast(FILE *stream, const struct printf_info *info, const void *const args[])
{
    ast_t *ast = *(ast_t**)(args[0]);
    if (ast) {
        if (info->alt)
            return fprintf(stream, "%.*s", (int)(ast->end - ast->start), ast->start);
        else
            return Text$print(stream, ast_to_xml(ast));
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
