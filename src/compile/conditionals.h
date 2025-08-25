// This file defines how to compile 'if' conditionals

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_condition(env_t *env, ast_t *ast);
Text_t compile_if_statement(env_t *env, ast_t *ast);
Text_t compile_if_expression(env_t *env, ast_t *ast);
