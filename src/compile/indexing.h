// This file defines how to compile indexing like `list[i]` or `ptr[]`

#pragma once

#include <stdbool.h>

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_indexing(env_t *env, ast_t *ast, bool checked);
