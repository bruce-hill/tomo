#pragma once

// Type-checking functions

#include <gc.h>
#include <stdarg.h>
#include <stdbool.h>

#include "ast.h"
#include "environment.h"
#include "types.h"

type_t *parse_type_ast(env_t *env, type_ast_t *ast);
type_t *get_type(env_t *env, ast_t *ast);
void prebind_statement(env_t *env, ast_t *statement);
void bind_statement(env_t *env, ast_t *statement);
PUREFUNC type_t *get_math_type(env_t *env, ast_t *ast, type_t *lhs_t, type_t *rhs_t);
PUREFUNC bool is_discardable(env_t *env, ast_t *ast);
type_t *get_function_type(env_t *env, ast_t *ast);
type_t *get_function_return_type(env_t *env, ast_t *ast);
type_t *get_arg_type(env_t *env, arg_t *arg);
type_t *get_arg_ast_type(env_t *env, arg_ast_t *arg);
env_t *match_clause_scope(env_t *env, type_t *subject_t, match_clause_t *clause);
PUREFUNC const char *record_literal_name(ast_t *type_expr);
PUREFUNC binding_t *get_variant_constructor(env_t *env, ast_t *type_expr);
type_t *get_clause_type(env_t *env, type_t *subject_t, match_clause_t *clause);
binding_t *get_subcommand_binding(env_t *env, ast_t *ast);
PUREFUNC bool can_be_mutated(env_t *env, ast_t *ast);
type_t *parse_type_string(env_t *env, const char *str);
type_t *get_method_type(env_t *env, ast_t *self, const char *name);
bool is_constant(env_t *env, ast_t *ast, type_t *expected_type);
PUREFUNC bool is_pushdown_arithmetic(ast_t *ast, type_t *target);
List_t get_embed_bytes(ast_t *ast);
bool embed_is_constant(ast_t *ast, type_t *t);
PUREFUNC bool can_compile_to_type(env_t *env, ast_t *ast, type_t *needed);
List_t get_field_names(env_t *env, type_t *t);
List_t get_method_names(env_t *env, type_t *t);
OptionalText_t suggest_best_name(const char *wrong, List_t names);

typedef struct {
    bool promotion : 1;
} call_opts_t;

void fail_if_serialize_shadowed(env_t *env, ast_t *ast);
void fail_if_optional_field_access(ast_t *ast, type_t *fielded_t, const char *field);
bool is_valid_call(env_t *env, arg_t *spec_args, arg_ast_t *call_args, call_opts_t options);
