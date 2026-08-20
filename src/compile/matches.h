// This file defines how to compile 'match' statements/expressions

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_match_statement(env_t *env, ast_t *ast);
Text_t compile_match_expression(env_t *env, ast_t *ast);
