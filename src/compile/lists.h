#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_typed_list(env_t *env, ast_t *ast, type_t *list_type);
Text_t compile_list_method_call(env_t *env, ast_t *ast);
