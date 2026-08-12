// This file defines how to compile text

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "../types.h"

Text_t compile_text_ast(env_t *env, ast_t *ast);
Text_t compile_text(env_t *env, ast_t *ast, Text_t color);
Text_t compile_text_literal(Text_t literal);
Text_t compile_bytes_literal(List_t bytes);
Text_t compile_embed_as_text(ast_t *ast, List_t bytes);
Text_t compile_embed_as_cstring(ast_t *ast, List_t bytes);
Text_t expr_as_text(Text_t expr, type_t *t, Text_t color);

MACROLIKE Text_t quoted_str(const char *str) {
    return Text$quoted(Text$from_str(str), false, Text("\""));
}
MACROLIKE Text_t quoted_text(Text_t text) {
    return Text$quoted(text, false, Text("\""));
}
