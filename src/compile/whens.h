// This file defines how to compile 'when' statements/expressions

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_when_statement(env_t *env, ast_t *ast);
Text_t compile_when_expression(env_t *env, ast_t *ast);
