#pragma once

// Compilation of user-defined structs

#include <gc/cord.h>

#include "ast.h"
#include "environment.h"

CORD compile_struct_typeinfo(env_t *env, type_t *t, const char *name, arg_ast_t *fields, bool is_secret, bool is_opaque);
CORD compile_struct_header(env_t *env, ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
