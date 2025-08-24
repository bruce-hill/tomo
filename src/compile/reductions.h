// This file defines how to compile reductions like `(+: nums)`
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_reduction(env_t *env, ast_t *ast);
