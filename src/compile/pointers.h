#include <stdbool.h>

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref);
Text_t compile_typed_allocation(env_t *env, ast_t *ast, type_t *pointer_type);
