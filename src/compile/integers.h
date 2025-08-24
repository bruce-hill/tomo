
#include "../ast.h"
#include "../environment.h"
#include "../types.h"

Text_t compile_int_to_type(env_t *env, ast_t *ast, type_t *target);
Text_t compile_int(ast_t *ast);
