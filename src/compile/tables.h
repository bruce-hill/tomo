// This file defines how to compile tables
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_typed_table(env_t *env, ast_t *ast, type_t *table_type);
Text_t compile_table_method_call(env_t *env, ast_t *ast);
