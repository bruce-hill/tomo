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
type_t *get_file_type(env_t *env, const char *path);
type_t *get_function_def_type(env_t *env, ast_t *ast);
type_t *get_arg_type(env_t *env, arg_t *arg);
type_t *get_arg_ast_type(env_t *env, arg_ast_t *arg);
bool can_be_mutated(env_t *env, ast_t *ast);
type_t *parse_type_string(env_t *env, const char *str);
type_t *get_method_type(env_t *env, ast_t *self, const char *name);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
