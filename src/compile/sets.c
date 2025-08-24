
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

static ast_t *add_to_set_comprehension(ast_t *item, ast_t *subject) {
    return WrapAST(item, MethodCall, .name = "add", .self = subject, .args = new (arg_ast_t, .value = item));
}

Text_t compile_typed_set(env_t *env, ast_t *ast, type_t *set_type) {
    DeclareMatch(set, ast, Set);
    if (!set->items) return Text("((Table_t){})");

    type_t *item_type = Match(set_type, SetType)->item_type;

    int64_t n = 0;
    for (ast_list_t *item = set->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension) goto set_comprehension;
    }

    { // No comprehension:
        Text_t code = Texts("Set(", compile_type(item_type), ", ", compile_type_info(item_type), ", ", String(n));
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        for (ast_list_t *item = set->items; item; item = item->next) {
            code = Texts(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return Texts(code, ")");
    }

set_comprehension: {
    static int64_t comp_num = 1;
    env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
    const char *comprehension_name = String("set$", comp_num++);
    ast_t *comprehension_var =
        LiteralCode(Texts("&", comprehension_name), .type = Type(PointerType, .pointed = set_type, .is_stack = true));
    Text_t code = Texts("({ Table_t ", comprehension_name, " = {};");
    Closure_t comp_action = {.fn = add_to_set_comprehension, .userdata = comprehension_var};
    scope->comprehension_action = &comp_action;
    for (ast_list_t *item = set->items; item; item = item->next) {
        if (item->ast->tag == Comprehension) code = Texts(code, "\n", compile_statement(scope, item->ast));
        else code = Texts(code, compile_statement(env, add_to_set_comprehension(item->ast, comprehension_var)));
    }
    code = Texts(code, " ", comprehension_name, "; })");
    return code;
}
}

public
Text_t compile_set_method_call(env_t *env, ast_t *ast) {
    DeclareMatch(call, ast, MethodCall);
    type_t *self_t = get_type(env, call->self);

    int64_t pointer_depth = 0;
    type_t *self_value_t = self_t;
    for (; self_value_t->tag == PointerType; self_value_t = Match(self_value_t, PointerType)->pointed)
        pointer_depth += 1;

    Text_t self = compile(env, call->self);
#define EXPECT_POINTER()                                                                                               \
    do {                                                                                                               \
        if (pointer_depth < 1) code_err(call->self, "I expected a set pointer here, not a set value");                 \
        else if (pointer_depth > 1) code_err(call->self, "I expected a set pointer here, not a nested set pointer");   \
    } while (0)
    DeclareMatch(set, self_value_t, SetType);
    if (streq(call->name, "has")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "key", .type = set->item_type);
        return Texts("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "add")) {
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "item", .type = set->item_type);
        return Texts("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", NULL, ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "add_all")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "items", .type = Type(ListType, .item_type = Match(self_value_t, SetType)->item_type));
        return Texts("({ Table_t *set = ", self, "; ",
                     "List_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                     "for (int64_t i = 0; i < to_add.length; i++)\n"
                     "Table$set(set, to_add.data + i*to_add.stride, NULL, ",
                     compile_type_info(self_value_t), ");\n", "(void)0; })");
    } else if (streq(call->name, "remove")) {
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "item", .type = set->item_type);
        return Texts("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "remove_all")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "items", .type = Type(ListType, .item_type = Match(self_value_t, SetType)->item_type));
        return Texts("({ Table_t *set = ", self, "; ",
                     "List_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                     "for (int64_t i = 0; i < to_add.length; i++)\n"
                     "Table$remove(set, to_add.data + i*to_add.stride, ",
                     compile_type_info(self_value_t), ");\n", "(void)0; })");
    } else if (streq(call->name, "clear")) {
        EXPECT_POINTER();
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("Table$clear(", self, ")");
    } else if (streq(call->name, "with")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "other", .type = self_value_t);
        return Texts("Table$with(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "overlap")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "other", .type = self_value_t);
        return Texts("Table$overlap(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "without")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "other", .type = self_value_t);
        return Texts("Table$without(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "is_subset_of")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec =
            new (arg_t, .name = "other", .type = self_value_t,
                 .next = new (arg_t, .name = "strict", .type = Type(BoolType), .default_val = FakeAST(Bool, false)));
        return Texts("Table$is_subset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "is_superset_of")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec =
            new (arg_t, .name = "other", .type = self_value_t,
                 .next = new (arg_t, .name = "strict", .type = Type(BoolType), .default_val = FakeAST(Bool, false)));
        return Texts("Table$is_superset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else code_err(ast, "There is no '", call->name, "' method for tables");
}
