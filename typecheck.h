#pragma once
#include <gc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "types.h"

type_t *parse_type_ast(env_t *env, type_ast_t *ast);
type_t *get_type(env_t *env, ast_t *ast);
void bind_statement(env_t *env, ast_t *statement);
type_t *get_math_type(env_t *env, ast_t *ast, type_t *lhs_t, type_t *rhs_t);
bool is_discardable(env_t *env, ast_t *ast);
type_t *get_namespace_type(env_t *env, ast_t *namespace_ast, type_t *type);
type_t *get_file_type(env_t *env, const char *path);
type_t *get_function_def_type(env_t *env, ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
