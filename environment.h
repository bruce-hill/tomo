#pragma once

#include <gc/cord.h>

#include "types.h"
#include "builtins/table.h"

typedef struct {
    CORD imports;
    CORD typedefs;
    CORD typecode;
    CORD fndefs;
    CORD staticdefs;
    CORD funcs;
    CORD typeinfos;
    CORD main;
} compilation_unit_t;

typedef struct {
    type_t *return_type;
    table_t *closure_scope;
    table_t *closed_vars;
} fn_ctx_t;

typedef struct loop_ctx_s {
    struct loop_ctx_s *next;
    const char *loop_name, *key_name, *value_name;
    CORD skip_label, stop_label;
} loop_ctx_t;

typedef struct {
    table_t *types, *globals, *locals;
    table_t *type_namespaces; // Map of type name -> namespace table
    compilation_unit_t *code;
    fn_ctx_t *fn_ctx;
    loop_ctx_t *loop_ctx;
    CORD scope_prefix;
    const char *comprehension_var;
} env_t;

typedef struct {
    CORD code;
    type_t *type;
} binding_t;

env_t *new_compilation_unit(void);
env_t *global_scope(env_t *env);
env_t *fresh_scope(env_t *env);
env_t *for_scope(env_t *env, ast_t *ast);
env_t *namespace_env(env_t *env, const char *namespace_name);
__attribute__((noreturn))
void compiler_err(file_t *f, const char *start, const char *end, const char *fmt, ...);
binding_t *get_binding(env_t *env, const char *name);
void set_binding(env_t *env, const char *name, binding_t *binding);
binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name);
#define code_err(ast, ...) compiler_err((ast)->file, (ast)->start, (ast)->end, __VA_ARGS__)

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
