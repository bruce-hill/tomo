// This file defines how to compile files
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_file(env_t *env, ast_t *ast);
