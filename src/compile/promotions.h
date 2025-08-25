// This file defines how to do type promotions during compilation

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../types.h"

bool promote(env_t *env, ast_t *ast, Text_t *code, type_t *actual, type_t *needed);
Text_t compile_to_type(env_t *env, ast_t *ast, type_t *t);
