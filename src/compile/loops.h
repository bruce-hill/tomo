// This file defines how to compile loops

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_for_loop(env_t *env, ast_t *ast);
Text_t compile_repeat(env_t *env, ast_t *ast);
Text_t compile_while(env_t *env, ast_t *ast);
Text_t compile_skip(env_t *env, ast_t *ast);
Text_t compile_stop(env_t *env, ast_t *ast);
