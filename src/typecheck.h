#pragma once

// Type-checking functions

#include <gc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "environment.h"
#include "stdlib/datatypes.h"
#include "types.h"

type_t *parse_type_ast(env_t *env, type_ast_t *ast);
type_t *get_type(env_t *env, ast_t *ast);
void prebind_statement(env_t *env, ast_t *statement);
void bind_statement(env_t *env, ast_t *statement);
PUREFUNC type_t *get_math_type(env_t *env, ast_t *ast, type_t *lhs_t, type_t *rhs_t);
PUREFUNC bool is_discardable(env_t *env, ast_t *ast);
type_t *get_function_def_type(env_t *env, ast_t *ast);
type_t *get_arg_type(env_t *env, arg_t *arg);
type_t *get_arg_ast_type(env_t *env, arg_ast_t *arg);
env_t *when_clause_scope(env_t *env, type_t *subject_t, when_clause_t *clause);
type_t *get_clause_type(env_t *env, type_t *subject_t, when_clause_t *clause);
PUREFUNC bool can_be_mutated(env_t *env, ast_t *ast);
type_t *parse_type_string(env_t *env, const char *str);
type_t *get_method_type(env_t *env, ast_t *self, const char *name);
PUREFUNC bool is_constant(env_t *env, ast_t *ast);
Table_t *get_arg_bindings(env_t *env, arg_t *spec_args, arg_ast_t *call_args, bool promotion_allowed);
bool is_valid_call(env_t *env, arg_t *spec_args, arg_ast_t *call_args, bool promotion_allowed);
PUREFUNC bool can_compile_to_type(env_t *env, ast_t *ast, type_t *needed);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
