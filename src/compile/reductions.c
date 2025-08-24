// This file defines how to compile reductions like `(+: nums)`

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_reduction(env_t *env, ast_t *ast) {
    DeclareMatch(reduction, ast, Reduction);
    ast_e op = reduction->op;

    type_t *iter_t = get_type(env, reduction->iter);
    type_t *item_t = get_iterated_type(iter_t);
    if (!item_t)
        code_err(reduction->iter, "I couldn't figure out how to iterate over this type: ", type_to_str(iter_t));

    static int64_t next_id = 1;
    ast_t *item = FakeAST(Var, String("$it", next_id++));
    ast_t *body = LiteralCode(Text("{}")); // placeholder
    ast_t *loop = FakeAST(For, .vars = new (ast_list_t, .ast = item), .iter = reduction->iter, .body = body);
    env_t *body_scope = for_scope(env, loop);
    if (op == Equals || op == NotEquals || op == LessThan || op == LessThanOrEquals || op == GreaterThan
        || op == GreaterThanOrEquals) {
        // Chained comparisons like ==, <, etc.
        type_t *item_value_type = item_t;
        ast_t *item_value = item;
        if (reduction->key) {
            set_binding(body_scope, "$", item_t, compile(body_scope, item));
            item_value = reduction->key;
            item_value_type = get_type(body_scope, reduction->key);
        }

        Text_t code = Texts("({ // Reduction:\n", compile_declaration(item_value_type, Text("prev")),
                            ";\n"
                            "OptionalBool_t result = NONE_BOOL;\n");

        ast_t *comparison =
            new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = op,
                 .__data.Plus.lhs = LiteralCode(Text("prev"), .type = item_value_type), .__data.Plus.rhs = item_value);
        body->__data.InlineCCode.chunks = new (
            ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (result == NONE_BOOL) {\n"
                                                          "    prev = ",
                                                          compile(body_scope, item_value),
                                                          ";\n"
                                                          "    result = yes;\n"
                                                          "} else {\n"
                                                          "    if (",
                                                          compile(body_scope, comparison), ") {\n",
                                                          "        prev = ", compile(body_scope, item_value), ";\n",
                                                          "    } else {\n"
                                                          "        result = no;\n",
                                                          "        break;\n", "    }\n", "}\n")));
        code = Texts(code, compile_statement(env, loop), "\nresult;})");
        return code;
    } else if (op == Min || op == Max) {
        // Min/max:
        Text_t superlative = op == Min ? Text("min") : Text("max");
        Text_t code = Texts("({ // Reduction:\n", compile_declaration(item_t, superlative),
                            ";\n"
                            "Bool_t has_value = no;\n");

        Text_t item_code = compile(body_scope, item);
        ast_e cmp_op = op == Min ? LessThan : GreaterThan;
        if (reduction->key) {
            env_t *key_scope = fresh_scope(env);
            set_binding(key_scope, "$", item_t, item_code);
            type_t *key_type = get_type(key_scope, reduction->key);
            Text_t superlative_key = op == Min ? Text("min_key") : Text("max_key");
            code = Texts(code, compile_declaration(key_type, superlative_key), ";\n");

            ast_t *comparison = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = cmp_op,
                                     .__data.Plus.lhs = LiteralCode(Text("key"), .type = key_type),
                                     .__data.Plus.rhs = LiteralCode(superlative_key, .type = key_type));

            body->__data.InlineCCode.chunks = new (
                ast_list_t, .ast = FakeAST(TextLiteral, Texts(compile_declaration(key_type, Text("key")), " = ",
                                                              compile(key_scope, reduction->key), ";\n",
                                                              "if (!has_value || ", compile(body_scope, comparison),
                                                              ") {\n"
                                                              "    ",
                                                              superlative, " = ", compile(body_scope, item),
                                                              ";\n"
                                                              "    ",
                                                              superlative_key,
                                                              " = key;\n"
                                                              "    has_value = yes;\n"
                                                              "}\n")));
        } else {
            ast_t *comparison =
                new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = cmp_op,
                     .__data.Plus.lhs = item, .__data.Plus.rhs = LiteralCode(superlative, .type = item_t));
            body->__data.InlineCCode.chunks = new (
                ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (!has_value || ", compile(body_scope, comparison),
                                                              ") {\n"
                                                              "    ",
                                                              superlative, " = ", compile(body_scope, item),
                                                              ";\n"
                                                              "    has_value = yes;\n"
                                                              "}\n")));
        }

        code = Texts(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(item_t, superlative),
                     " : ", compile_none(item_t), ";})");
        return code;
    } else {
        // Accumulator-style reductions like +, ++, *, etc.
        type_t *reduction_type = Match(get_type(env, ast), OptionalType)->type;
        ast_t *item_value = item;
        if (reduction->key) {
            set_binding(body_scope, "$", item_t, compile(body_scope, item));
            item_value = reduction->key;
        }

        Text_t code = Texts("({ // Reduction:\n", compile_declaration(reduction_type, Text("reduction")),
                            ";\n"
                            "Bool_t has_value = no;\n");

        // For the special case of (or)/(and), we need to early out if we
        // can:
        Text_t early_out = EMPTY_TEXT;
        if (op == Compare) {
            if (reduction_type->tag != IntType || Match(reduction_type, IntType)->bits != TYPE_IBITS32)
                code_err(ast, "<> reductions are only supported for Int32 "
                              "values");
        } else if (op == And) {
            if (reduction_type->tag == BoolType) early_out = Text("if (!reduction) break;");
            else if (reduction_type->tag == OptionalType)
                early_out = Texts("if (", check_none(reduction_type, Text("reduction")), ") break;");
        } else if (op == Or) {
            if (reduction_type->tag == BoolType) early_out = Text("if (reduction) break;");
            else if (reduction_type->tag == OptionalType)
                early_out = Texts("if (!", check_none(reduction_type, Text("reduction")), ") break;");
        }

        ast_t *combination = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = op,
                                  .__data.Plus.lhs = LiteralCode(Text("reduction"), .type = reduction_type),
                                  .__data.Plus.rhs = item_value);
        body->__data.InlineCCode.chunks = new (
            ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (!has_value) {\n"
                                                          "    reduction = ",
                                                          compile(body_scope, item_value),
                                                          ";\n"
                                                          "    has_value = yes;\n"
                                                          "} else {\n"
                                                          "    reduction = ",
                                                          compile(body_scope, combination), ";\n", early_out, "}\n")));

        code =
            Texts(code, compile_statement(env, loop), "\nhas_value ? ",
                  promote_to_optional(reduction_type, Text("reduction")), " : ", compile_none(reduction_type), ";})");
        return code;
    }
}
