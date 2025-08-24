// Compilation logic for lists

#include <gc.h>
#include <glob.h>
#include <gmp.h>
#include <uninorm.h>

#include "../ast.h"
#include "../compile.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"

static ast_t *add_to_list_comprehension(ast_t *item, ast_t *subject) {
    return WrapAST(item, MethodCall, .name = "insert", .self = subject, .args = new (arg_ast_t, .value = item));
}

public
Text_t compile_typed_list(env_t *env, ast_t *ast, type_t *list_type) {
    DeclareMatch(list, ast, List);
    if (!list->items) return Text("(List_t){.length=0}");

    type_t *item_type = Match(list_type, ListType)->item_type;

    int64_t n = 0;
    for (ast_list_t *item = list->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension) goto list_comprehension;
    }

    {
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        if (is_incomplete_type(item_type)) code_err(ast, "This list's type can't be inferred!");
        Text_t code = Texts("TypedListN(", compile_type(item_type), ", ", String(n));
        for (ast_list_t *item = list->items; item; item = item->next) {
            code = Texts(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return Texts(code, ")");
    }

list_comprehension: {
    env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
    static int64_t comp_num = 1;
    const char *comprehension_name = String("list$", comp_num++);
    ast_t *comprehension_var =
        LiteralCode(Texts("&", comprehension_name), .type = Type(PointerType, .pointed = list_type, .is_stack = true));
    Closure_t comp_action = {.fn = add_to_list_comprehension, .userdata = comprehension_var};
    scope->comprehension_action = &comp_action;
    Text_t code = Texts("({ List_t ", comprehension_name, " = {};");
    // set_binding(scope, comprehension_name, list_type, comprehension_name);
    for (ast_list_t *item = list->items; item; item = item->next) {
        if (item->ast->tag == Comprehension) code = Texts(code, "\n", compile_statement(scope, item->ast));
        else code = Texts(code, compile_statement(env, add_to_list_comprehension(item->ast, comprehension_var)));
    }
    code = Texts(code, " ", comprehension_name, "; })");
    return code;
}
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
