
#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_text_ast(env_t *env, ast_t *ast);
Text_t compile_text(env_t *env, ast_t *ast, Text_t color);
Text_t compile_text_literal(Text_t literal);
Text_t expr_as_text(Text_t expr, type_t *t, Text_t color);
