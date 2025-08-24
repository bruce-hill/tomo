// This file defines how to compile doctests
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_doctest(env_t *env, ast_t *ast);
