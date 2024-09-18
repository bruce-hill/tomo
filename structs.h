#pragma once

// Compilation of user-defined structs

#include <gc/cord.h>

#include "ast.h"
#include "environment.h"

void compile_struct_def(env_t *env, ast_t *ast);
CORD compile_struct_header(env_t *env, ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
