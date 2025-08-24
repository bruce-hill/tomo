
#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_update_assignment(env_t *env, ast_t *ast);
Text_t compile_declaration(type_t *t, Text_t name);
Text_t compile_declared_value(env_t *env, ast_t *declare_ast);
Text_t compile_assignment(env_t *env, ast_t *target, Text_t value);
Text_t compile_lvalue(env_t *env, ast_t *ast);
