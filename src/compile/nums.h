// This file defines how to compile Num literals

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_num_value(Num_t n, Text_t fallback);
Text_t compile_num_to_type(env_t *env, ast_t *ast, type_t *target);
Text_t compile_num(ast_t *ast);
