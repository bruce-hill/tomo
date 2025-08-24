#include <stdbool.h>

#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref);
