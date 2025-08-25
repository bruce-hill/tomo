// This file defines how to compile comparisons

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_comparison(env_t *env, ast_t *ast);
