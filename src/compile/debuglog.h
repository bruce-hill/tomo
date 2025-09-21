// This file defines how to compile debug logs

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_debug_log(env_t *env, ast_t *ast);
