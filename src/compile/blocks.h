
#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_block(env_t *env, ast_t *ast);
Text_t compile_block_expression(env_t *env, ast_t *ast);
Text_t compile_inline_block(env_t *env, ast_t *ast);
