
#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_function_call(env_t *env, ast_t *ast);
Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args);
