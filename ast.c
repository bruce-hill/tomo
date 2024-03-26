// Some basic operations defined on AST nodes, mainly converting to
// strings for debugging.
#include <gc/cord.h>
#include <stdarg.h>
#include <printf.h>

#include "ast.h"
#include "builtins/text.h"

static const char *OP_NAMES[] = {
    [BINOP_UNKNOWN]="unknown",
    [BINOP_POWER]="^", [BINOP_MULT]="*", [BINOP_DIVIDE]="/",
    [BINOP_MOD]="mod", [BINOP_MOD1]="mod1", [BINOP_PLUS]="+", [BINOP_MINUS]="minus",
    [BINOP_CONCAT]="++", [BINOP_LSHIFT]="<<", [BINOP_RSHIFT]=">>", [BINOP_MIN]="min",
    [BINOP_MAX]="max", [BINOP_EQ]="==", [BINOP_NE]="!=", [BINOP_LT]="<",
    [BINOP_LE]="<=", [BINOP_GT]=">", [BINOP_GE]=">=", [BINOP_AND]="and", [BINOP_OR]="or", [BINOP_XOR]="xor",
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
    text = Text__replace(text, "&", "&amp;", INT64_MAX);
    text = Text__replace(text, "<", "&lt;", INT64_MAX);
    text = Text__replace(text, ">", "&gt;", INT64_MAX);
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
        c = CORD_all(c, "<case");
        if (clauses->var) c = CORD_all(c, " var=\"", Match(clauses->var, Var)->name, "\"");
        c = CORD_all(c, " tag=\"", ast_to_xml(clauses->tag_name), "\">", ast_to_xml(clauses->body), "</case>");
    }
    return c;
}

CORD tags_to_xml(tag_ast_t *tags) {
    CORD c = CORD_EMPTY;
    for (; tags; tags = tags->next) {
        c = CORD_all(c, "<tag name=\"", tags->name, "\" value=\"", CORD_asprintf("%ld", tags->value), "\">", arg_list_to_xml(tags->fields), "</tag>");
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
    T(Nil, "<Nil>%r</Nil>", type_ast_to_xml(data.type))
    T(Bool, "<Bool value=\"%s\" />", data.b ? "yes" : "no")
    T(Var, "%s", data.name)
    T(Int, "<Int bits=\"%ld\">%ld</Int>", data.bits, data.i)
    T(Num, "<Num bits=\"%ld\">%g</Num>", data.bits, data.n)
    T(TextLiteral, "%r", xml_escape(data.cord))
    T(TextJoin, "<Text%r>%r</Text>", data.lang ? CORD_all(" lang=\"", data.lang, "\"") : CORD_EMPTY, ast_list_to_xml(data.children))
    T(Declare, "<Declare var=\"%r\">%r</Declare>", ast_to_xml(data.var), ast_to_xml(data.value))
    T(Assign, "<Assign><targets>%r</targets><values>%r</values></Assign>", ast_list_to_xml(data.targets), ast_list_to_xml(data.values))
    T(BinaryOp, "<BinaryOp op=\"%r\">%r %r</BinaryOp>", xml_escape(OP_NAMES[data.op]), ast_to_xml(data.lhs), ast_to_xml(data.rhs))
    T(UpdateAssign, "<UpdateAssign op=\"%r\">%r %r</UpdateAssign>", xml_escape(OP_NAMES[data.op]), ast_to_xml(data.lhs), ast_to_xml(data.rhs))
    T(Length, "<Length>%r</Length>", ast_to_xml(data.value))
    T(Negative, "<Negative>%r</Negative>", ast_to_xml(data.value))
    T(Not, "<Not>%r</Not>", ast_to_xml(data.value))
    T(HeapAllocate, "<HeapAllocate>%r</HeapAllocate>", ast_to_xml(data.value))
    T(StackReference, "<StackReference>%r</StackReference>", ast_to_xml(data.value))
    T(Min, "<Min>%r%r%r</Min>", ast_to_xml(data.lhs), ast_to_xml(data.rhs), optional_tagged("key", data.key))
    T(Max, "<Max>%r%r%r</Max>", ast_to_xml(data.lhs), ast_to_xml(data.rhs), optional_tagged("key", data.key))
    T(Array, "<Array>%r%r</Array>", optional_tagged_type("item-type", data.type), ast_list_to_xml(data.items))
    T(Table, "<Table>%r%r%r%r%r</Table>",
      optional_tagged_type("key-type", data.key_type), optional_tagged_type("value-type", data.value_type),
      ast_list_to_xml(data.entries), optional_tagged("fallback", data.fallback),
      optional_tagged("default", data.default_value))
    T(TableEntry, "<TableEntry>%r%r</TableEntry>", ast_to_xml(data.key), ast_to_xml(data.value))
    T(Comprehension, "<Comprehension>%r%r%r%r%r</Comprehension>", optional_tagged("expr", data.expr),
      optional_tagged("key", data.key), optional_tagged("value", data.value), optional_tagged("iter", data.iter),
      optional_tagged("filter", data.filter))
    T(FunctionDef, "<FunctionDef name=\"%r\">%r%r<body>%r</body></FunctionDef>", ast_to_xml(data.name),
      arg_list_to_xml(data.args), optional_tagged_type("return-type", data.ret_type), ast_to_xml(data.body))
    T(Lambda, "<Lambda>%r<body>%r</body></Lambda>)", arg_list_to_xml(data.args), ast_to_xml(data.body))
    T(FunctionCall, "<FunctionCall><function>%r</function>%r</FunctionCall>", ast_to_xml(data.fn), arg_list_to_xml(data.args))
    T(MethodCall, "<MethodCall><self>%r</self><method>%s</method>%r</MethodCall>", ast_to_xml(data.self), data.name, arg_list_to_xml(data.args))
    T(Block, "<Block>%r</Block>", ast_list_to_xml(data.statements))
    T(For, "<For>%r%r%r%r%r</For>", optional_tagged("index", data.index), optional_tagged("value", data.value),
      optional_tagged("iterable", data.iter), optional_tagged("body", data.body), optional_tagged("empty", data.empty))
    T(While, "<While>%r%r</While>", optional_tagged("condition", data.condition), optional_tagged("body", data.body))
    T(If, "<If>%r%r%r</If>", optional_tagged("condition", data.condition), optional_tagged("body", data.body), optional_tagged("else", data.else_body))
    T(When, "<When><subject>%r</subject>%r%r</When>", ast_to_xml(data.subject), when_clauses_to_xml(data.clauses), optional_tagged("else", data.else_body))
    T(Reduction, "<Reduction>%r%r%r</Reduction>", optional_tagged("iterable", data.iter),
      optional_tagged("combination", data.combination), optional_tagged("fallback", data.fallback))
    T(Skip, "<Skip>%r</Skip>", data.target)
    T(Stop, "<Stop>%s</Stop>", data.target)
    T(Pass, "<Pass/>")
    T(Return, "<Return>%r</Return>", ast_to_xml(data.value))
    T(Extern, "<Extern name=\"%s\">%r</Extern>", data.name, type_ast_to_xml(data.type))
    T(StructDef, "<StructDef name=\"%s\">%r<namespace>%r</namespace></StructDef>", data.name, arg_list_to_xml(data.fields), ast_to_xml(data.namespace))
    T(EnumDef, "<EnumDef name=\"%s\"><tags>%r</tags><namespace>%r</namespace></EnumDef>", data.name, tags_to_xml(data.tags), ast_to_xml(data.namespace))
    T(LangDef, "<LangDef name=\"%s\">%r</LangDef>", data.name, ast_to_xml(data.namespace))
    T(Index, "<Index>%r%r</Index>", optional_tagged("indexed", data.indexed), optional_tagged("index", data.index))
    T(FieldAccess, "<FieldAccess field=\"%s\">%r</FieldAccess>", data.field, ast_to_xml(data.fielded))
    T(DocTest, "<DocTest>%r<output>%r</output></DocTest>", optional_tagged("expression", data.expr), xml_escape(data.output))
    T(Use, "<Use>%r</Use>", xml_escape(data.raw_path))
    T(LinkerDirective, "<LinkerDirective>%r</LinkerDirective>", xml_escape(data.directive))
    T(InlineCCode, "<InlineCode>%r</InlineCode>", xml_escape(data.code))
#undef T
    }
    return "???";
}

CORD type_ast_to_xml(type_ast_t *t)
{
    if (!t) return "\x1b[35mNULL\x1b[m";

    switch (t->tag) {
#define T(type, ...) case type: { auto data = t->__data.type; (void)data; return CORD_asprintf(__VA_ARGS__); }
    T(UnknownTypeAST, "<UnknownType/>")
    T(VarTypeAST, "%s", data.name)
    T(PointerTypeAST, "<PointerType is_optional=\"%d\", is_stack=\"%d\", is_readonly=\"%d\">%r</PointerType>",
      data.is_optional, data.is_stack, data.is_readonly, type_ast_to_xml(data.pointed))
    T(ArrayTypeAST, "<ArrayType>%r</ArrayType>", type_ast_to_xml(data.item))
    T(TableTypeAST, "<TableType>%r %r</TableType>", type_ast_to_xml(data.key), type_ast_to_xml(data.value))
    T(FunctionTypeAST, "<FunctionType>%r %r</FunctionType>", arg_list_to_xml(data.args), type_ast_to_xml(data.ret))
#undef T
    }
    return NULL;
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

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
