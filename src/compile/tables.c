
#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../types.h"
#include "promotion.h"

static ast_t *add_to_table_comprehension(ast_t *entry, ast_t *subject) {
    DeclareMatch(e, entry, TableEntry);
    return WrapAST(entry, MethodCall, .name = "set", .self = subject,
                   .args = new (arg_ast_t, .value = e->key, .next = new (arg_ast_t, .value = e->value)));
}

Text_t compile_typed_table(env_t *env, ast_t *ast, type_t *table_type) {
    DeclareMatch(table, ast, Table);
    if (!table->entries) {
        Text_t code = Text("((Table_t){");
        if (table->fallback) code = Texts(code, ".fallback=heap(", compile(env, table->fallback), ")");
        return Texts(code, "})");
    }

    type_t *key_t = Match(table_type, TableType)->key_type;
    type_t *value_t = Match(table_type, TableType)->value_type;

    if (value_t->tag == OptionalType)
        code_err(ast, "Tables whose values are optional (", type_to_str(value_t), ") are not currently supported.");

    for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
        if (entry->ast->tag == Comprehension) goto table_comprehension;
    }

    { // No comprehension:
        env_t *key_scope = key_t->tag == EnumType ? with_enum_scope(env, key_t) : env;
        env_t *value_scope = value_t->tag == EnumType ? with_enum_scope(env, value_t) : env;
        Text_t code = Texts("Table(", compile_type(key_t), ", ", compile_type(value_t), ", ", compile_type_info(key_t),
                            ", ", compile_type_info(value_t));
        if (table->fallback) code = Texts(code, ", /*fallback:*/ heap(", compile(env, table->fallback), ")");
        else code = Texts(code, ", /*fallback:*/ NULL");

        size_t n = 0;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next)
            ++n;
        code = Texts(code, ", ", String((int64_t)n));

        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            DeclareMatch(e, entry->ast, TableEntry);
            code = Texts(code, ",\n\t{", compile_to_type(key_scope, e->key, key_t), ", ",
                         compile_to_type(value_scope, e->value, value_t), "}");
        }
        return Texts(code, ")");
    }

table_comprehension: {
    static int64_t comp_num = 1;
    env_t *scope = fresh_scope(env);
    const char *comprehension_name = String("table$", comp_num++);
    ast_t *comprehension_var =
        LiteralCode(Texts("&", comprehension_name), .type = Type(PointerType, .pointed = table_type, .is_stack = true));

    Text_t code = Texts("({ Table_t ", comprehension_name, " = {");
    if (table->fallback) code = Texts(code, ".fallback=heap(", compile(env, table->fallback), "), ");

    code = Texts(code, "};");

    Closure_t comp_action = {.fn = add_to_table_comprehension, .userdata = comprehension_var};
    scope->comprehension_action = &comp_action;
    for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
        if (entry->ast->tag == Comprehension) code = Texts(code, "\n", compile_statement(scope, entry->ast));
        else code = Texts(code, compile_statement(env, add_to_table_comprehension(entry->ast, comprehension_var)));
    }
    code = Texts(code, " ", comprehension_name, "; })");
    return code;
}
}
