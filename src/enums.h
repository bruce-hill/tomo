#pragma once

// Compilation of tagged unions (enums)

#include <gc/cord.h>

#include "ast.h"
#include "environment.h"

CORD compile_enum_typeinfo(env_t *env, ast_t *ast);
CORD compile_enum_constructors(env_t *env, ast_t *ast);
CORD compile_enum_header(env_t *env, ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
