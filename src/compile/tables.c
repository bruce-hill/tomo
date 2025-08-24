
#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "functions.h"
#include "optionals.h"
#include "pointers.h"
#include "promotions.h"
#include "statements.h"
#include "types.h"

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

public
Text_t compile_table_method_call(env_t *env, ast_t *ast) {
    DeclareMatch(call, ast, MethodCall);
    type_t *self_t = get_type(env, call->self);

    int64_t pointer_depth = 0;
    type_t *self_value_t = self_t;
    for (; self_value_t->tag == PointerType; self_value_t = Match(self_value_t, PointerType)->pointed)
        pointer_depth += 1;

    Text_t self = compile(env, call->self);

#define EXPECT_POINTER()                                                                                               \
    do {                                                                                                               \
        if (pointer_depth < 1) code_err(call->self, "I expected a table pointer here, not a table value");             \
        else if (pointer_depth > 1)                                                                                    \
            code_err(call->self, "I expected a table pointer here, not a nested table pointer");                       \
    } while (0)

    DeclareMatch(table, self_value_t, TableType);
    if (streq(call->name, "get")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "key", .type = table->key_type);
        return Texts("Table$get_optional(", self, ", ", compile_type(table->key_type), ", ",
                     compile_type(table->value_type), ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     "_, ", optional_into_nonnone(table->value_type, Text("(*_)")), ", ",
                     compile_none(table->value_type), ", ", compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "get_or_set")) {
        self = compile_to_pointer_depth(env, call->self, 1, false);
        arg_t *arg_spec = new (
            arg_t, .name = "key", .type = table->key_type,
            .next = new (arg_t, .name = "default", .type = table->value_type, .default_val = table->default_value));
        return Texts("*Table$get_or_setdefault(", self, ", ", compile_type(table->key_type), ", ",
                     compile_type(table->value_type), ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "has")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "key", .type = table->key_type);
        return Texts("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "set")) {
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "key", .type = table->key_type,
                               .next = new (arg_t, .name = "value", .type = table->value_type));
        return Texts("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "remove")) {
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "key", .type = table->key_type);
        return Texts("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "clear")) {
        EXPECT_POINTER();
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("Table$clear(", self, ")");
    } else if (streq(call->name, "sorted")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("Table$sorted(", self, ", ", compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "with_fallback")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "fallback", .type = Type(OptionalType, self_value_t));
        return Texts("Table$with_fallback(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
    } else code_err(ast, "There is no '", call->name, "' method for tables");
}
