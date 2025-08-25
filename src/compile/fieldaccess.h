// This file defines how to compile field accessing like `foo.x`

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_field_access(env_t *env, ast_t *ast);
