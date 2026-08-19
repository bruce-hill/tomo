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
env_t *when_clause_scope(env_t *env, type_t *subject_t, when_clause_t *clause);
type_t *get_clause_type(env_t *env, type_t *subject_t, when_clause_t *clause);
PUREFUNC bool can_be_mutated(env_t *env, ast_t *ast);
type_t *parse_type_string(env_t *env, const char *str);
type_t *get_method_type(env_t *env, ast_t *self, const char *name);
bool is_constant(env_t *env, ast_t *ast, type_t *expected_type);
PUREFUNC bool is_pushdown_arithmetic(ast_t *ast, type_t *target);
List_t get_embed_bytes(ast_t *ast);
bool embed_is_constant(ast_t *ast, type_t *t);
PUREFUNC bool can_compile_to_type(env_t *env, ast_t *ast, type_t *needed);
OptionalText_t suggest_best_name(const char *wrong, List_t names);
List_t get_field_names(env_t *env, type_t *t);
List_t get_method_names(env_t *env, type_t *t);

typedef struct {
    bool promotion : 1, underscores : 1;
    // When set, a `[Byte]` argument may NOT be matched to a differently-typed
    // parameter by implicit (de)serialization (nor a value serialized into a
    // `[Byte]` parameter). Constructors set this so that, e.g., a `[Byte]`
    // doesn't silently deserialize to fill a `Path`/`CString` argument -- which
    // otherwise makes text interpolation of a byte list deserialize it. The
    // explicit `x : T = bytes` conversion is unaffected (it goes through
    // promote(), not constructor matching).
    bool no_serialization : 1;
} call_opts_t;

bool is_valid_call(env_t *env, arg_t *spec_args, arg_ast_t *call_args, call_opts_t options);
