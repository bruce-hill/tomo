// This file defines how to compile variable declarations
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_declaration(type_t *t, Text_t name);
Text_t compile_declared_value(env_t *env, ast_t *declare_ast);
