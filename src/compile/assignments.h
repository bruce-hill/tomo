// This file defines how to compile assignments
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_update_assignment(env_t *env, ast_t *ast);
Text_t compile_assignment(env_t *env, ast_t *target, Text_t value);
Text_t compile_lvalue(env_t *env, ast_t *ast);
