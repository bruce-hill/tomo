// This file defines how to compile `for` loops
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_for_loop(env_t *env, ast_t *ast);
