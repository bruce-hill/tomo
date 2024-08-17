#pragma once

// Compilation environments

#include <gc/cord.h>

#include "types.h"
#include "builtins/table.h"

typedef struct {
    CORD local_typedefs;
    CORD staticdefs;
    CORD funcs;
    CORD typeinfos;
    CORD variable_initializers;
} compilation_unit_t;

typedef struct fn_ctx_s {
    struct fn_ctx_s *parent;
    type_t *return_type;
    table_t *closure_scope;
    table_t *closed_vars;
} fn_ctx_t;

typedef struct deferral_s {
    struct deferral_s *next;
    struct env_s *defer_env;
    ast_t *block;
} deferral_t;

typedef struct loop_ctx_s {
    struct loop_ctx_s *next;
    const char *loop_name;
    ast_list_t *loop_vars;
    deferral_t *deferred;
    CORD skip_label, stop_label;
} loop_ctx_t;

typedef struct namespace_s {
    const char *name;
    struct namespace_s *parent;
} namespace_t;

typedef struct env_s {
    table_t *types, *globals, *locals;
    table_t *imports; // Map of 'use' name -> env_t*
    compilation_unit_t *code;
    fn_ctx_t *fn_ctx;
    loop_ctx_t *loop_ctx;
    deferral_t *deferred;
    CORD *libname; // Pointer to currently compiling library name (if any)
    namespace_t *namespace;
    const char *comprehension_var;
} env_t;

typedef struct {
    type_t *type;
    union {
        CORD code;
        void *value;
    };
} binding_t;

env_t *new_compilation_unit(CORD *libname);
env_t *load_module_env(env_t *env, ast_t *ast);
CORD namespace_prefix(CORD *libname, namespace_t *ns);
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
extern type_t *TEXT_TYPE;
extern type_t *RANGE_TYPE;
extern type_t *THREAD_TYPE;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
