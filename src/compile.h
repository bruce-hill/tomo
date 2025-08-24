#pragma once

// Compilation functions

#include "ast.h"
#include "environment.h"
#include "stdlib/datatypes.h"
#include "types.h"

Text_t compile(env_t *env, ast_t *ast);
Text_t compile_empty(type_t *t);
Text_t compile_maybe_incref(env_t *env, ast_t *ast, type_t *t);
