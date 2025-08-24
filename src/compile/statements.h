// This file defines how to compile statements
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_statement(env_t *env, ast_t *ast);
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code);
