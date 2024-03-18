#pragma once

// Compilation of tagged unions (enums)

#include <gc/cord.h>

#include "ast.h"
#include "environment.h"

void compile_enum_def(env_t *env, ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
