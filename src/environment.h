// Compilation environments

#pragma once

#include "stdlib/datatypes.h"
#include "stdlib/print.h" // IWYU pragma: export
#include "stdlib/stdlib.h" // IWYU pragma: export
#include "types.h"

typedef struct {
    Text_t local_typedefs;
    Text_t staticdefs;
    Text_t lambdas;
    Text_t variable_initializers;
} compilation_unit_t;

typedef struct loop_ctx_s {
    struct loop_ctx_s *next;
    const char *loop_name;
    ast_list_t *loop_vars;
    Text_t skip_label, stop_label;
} loop_ctx_t;

typedef struct namespace_s {
    const char *name;
    List_t constructors;
    struct namespace_s *parent;
} namespace_t;

typedef struct env_s {
    Table_t *types, *globals, *namespace_bindings, *locals;
    // Lookup table for env_t* where the key is:
    //  - Resolved path for local imports (so that `use ./foo.tm` is the same as `use ./baz/../foo.tm`)
    //  - Raw 'use' string for module imports
    namespace_t *namespace;
    Text_t id_suffix;
    Table_t *imports;
    compilation_unit_t *code;
    ast_t *fn;
    loop_ctx_t *loop_ctx;
    Closure_t *comprehension_action;
    bool do_source_mapping : 1;
    type_t *current_type;
} env_t;

typedef struct {
    type_t *type;
    Text_t code;
} binding_t;

env_t *global_env(bool source_mapping);
env_t *load_module_env(env_t *env, ast_t *ast);
env_t *get_namespace_by_type(env_t *env, type_t *t);
env_t *fresh_scope(env_t *env);
env_t *for_scope(env_t *env, ast_t *ast);
env_t *with_enum_scope(env_t *env, type_t *t);
env_t *namespace_env(env_t *env, const char *namespace_name);
#define compiler_err(f, start, end, ...)                                                                               \
    ({                                                                                                                 \
        file_t *_f = f;                                                                                                \
        if (USE_COLOR) fputs("\x1b[31;7;1m ", stderr);                                                                 \
        if (_f && start && end)                                                                                        \
            fprint_inline(stderr, _f->relative_filename, ":", get_line_number(_f, start), ".",                         \
                          get_line_column(_f, start), ": ");                                                           \
        fprint_inline(stderr, __VA_ARGS__);                                                                            \
        if (USE_COLOR) fputs(" \x1b[m", stderr);                                                                       \
        fputs("\n\n", stderr);                                                                                         \
        if (_f && start && end) highlight_error(_f, start, end, "\x1b[31;1m", 2, USE_COLOR);                           \
        if (getenv("TOMO_STACKTRACE")) print_stacktrace(stderr, 1);                                                    \
        raise(SIGABRT);                                                                                                \
        exit(1);                                                                                                       \
    })
binding_t *get_binding(env_t *env, const char *name);
binding_t *get_constructor(env_t *env, type_t *t, arg_ast_t *args, bool allow_underscores);
PUREFUNC binding_t *get_metamethod_binding(env_t *env, ast_e tag, ast_t *lhs, ast_t *rhs, type_t *ret);
void set_binding(env_t *env, const char *name, type_t *type, Text_t code);
binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name);
#define code_err(ast, ...) compiler_err((ast)->file, (ast)->start, (ast)->end, __VA_ARGS__)
extern type_t *TEXT_TYPE;
extern type_t *PATH_TYPE;
extern type_t *PATH_TYPE_TYPE;
