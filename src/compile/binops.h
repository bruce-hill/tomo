// This file defines how to compile binary operations
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_binary_op(env_t *env, ast_t *ast);
