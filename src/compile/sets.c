
#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../types.h"
#include "promotion.h"

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
