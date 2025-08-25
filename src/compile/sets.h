// This file defines how to compile sets

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_typed_set(env_t *env, ast_t *ast, type_t *set_type);
Text_t compile_set_method_call(env_t *env, ast_t *ast);
