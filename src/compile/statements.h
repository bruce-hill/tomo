#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_condition(env_t *env, ast_t *ast);
Text_t compile_inline_block(env_t *env, ast_t *ast);
Text_t compile_statement(env_t *env, ast_t *ast);
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code);
