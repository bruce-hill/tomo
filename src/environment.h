#pragma once

// Compilation environments

#include <gc/cord.h>

#include "types.h"
#include "stdlib/print.h"
#include "stdlib/stdlib.h"
#include "stdlib/tables.h"

typedef struct {
    CORD local_typedefs;
    CORD staticdefs;
    CORD lambdas;
    CORD variable_initializers;
    CORD function_naming;
} compilation_unit_t;

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
    Array_t constructors;
    struct namespace_s *parent;
} namespace_t;

typedef struct env_s {
    Table_t *types, *globals, *namespace_bindings, *locals;
    // Lookup table for env_t* where the key is:
    //  - Resolved path for local imports (so that `use ./foo.tm` is the same as `use ./baz/../foo.tm`)
    //  - Raw 'use' string for module imports
    Table_t *imports;
    compilation_unit_t *code;
    type_t *fn_ret;
    loop_ctx_t *loop_ctx;
    deferral_t *deferred;
    CORD libname; // Currently compiling library name (if any)
    namespace_t *namespace;
    Closure_t *comprehension_action;
} env_t;

typedef struct {
    type_t *type;
    CORD code;
} binding_t;

env_t *global_env(void);
env_t *load_module_env(env_t *env, ast_t *ast);
CORD namespace_prefix(env_t *env, namespace_t *ns);
env_t *get_namespace_by_type(env_t *env, type_t *t);
env_t *namespace_scope(env_t *env);
env_t *fresh_scope(env_t *env);
env_t *for_scope(env_t *env, ast_t *ast);
env_t *with_enum_scope(env_t *env, type_t *t);
env_t *namespace_env(env_t *env, const char *namespace_name);
#define compiler_err(f, start, end, ...) ({ \
    file_t *_f = f; \
    if (USE_COLOR) \
        fputs("\x1b[31;7;1m", stderr); \
    if (_f && start && end) \
        fprint_inline(stderr, _f->relative_filename, ":", get_line_number(_f, start), ".", get_line_column(_f, start), ": "); \
    fprint(stderr, __VA_ARGS__); \
    if (USE_COLOR) \
        fputs(" \x1b[m", stderr); \
    fputs("\n\n", stderr); \
    if (_f && start && end) \
        highlight_error(_f, start, end, "\x1b[31;1m", 2, USE_COLOR); \
    if (getenv("TOMO_STACKTRACE")) \
        print_stack_trace(stderr, 1, 3); \
    raise(SIGABRT); \
    exit(1); \
})
binding_t *get_binding(env_t *env, const char *name);
binding_t *get_constructor(env_t *env, type_t *t, arg_ast_t *args);
void set_binding(env_t *env, const char *name, type_t *type, CORD code);
binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name);
#define code_err(ast, ...) compiler_err((ast)->file, (ast)->start, (ast)->end, __VA_ARGS__)
extern type_t *TEXT_TYPE;
extern type_t *RNG_TYPE;
extern type_t *PATH_TYPE;
extern type_t *PATH_TYPE_TYPE;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
