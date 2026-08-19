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
    // Per-function-type closure shims (see promote() in compile/promotions.c),
    // keyed by the compiled function-pointer type string:
    Table_t closure_shims;
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
    Table_t *build_info;
    compilation_unit_t *code;
    ast_t *fn;
    loop_ctx_t *loop_ctx;
    deferral_t *deferred;
    Closure_t *comprehension_action;
    // List variables whose copy-on-write guard has been hoisted out of an
    // enclosing loop (see cow_hoist_env in compile/loops.c): keys are variable
    // names; indexed writes to them compile to List_lvalue_nocow. NULL when no
    // hoist is active.
    Table_t *cow_hoisted;
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
// Return a for-loop's single value variable (or NULL if there are no
// variables). Loop variables bind the values an iterable yields; iteration
// counters are bound separately with `at` (`for x at i in xs`). Raises a
// compile error if there is more than one variable, since every currently
// iterable value yields exactly one value per iteration. This is the one
// place the loop-variable arity rule lives.
ast_t *single_loop_var(ast_list_t *vars);
// If `iter_value_t` is a function/closure following the multi-value iterator
// protocol -- every argument is a non-escaping `&` out-parameter and the
// return type is Bool (each call either writes the next values through the
// out-refs and returns `yes`, or returns `no`) -- return its argument list.
// Otherwise return NULL. The number of arguments is the number of values the
// iterator yields per iteration.
arg_t *iterator_yield_args(type_t *iter_value_t);
arg_t *iteration_slots(env_t *env, ast_t *iter_ast);
env_t *with_enum_scope(env_t *env, type_t *t);
env_t *namespace_env(env_t *env, const char *namespace_name);
#define compiler_err(f, start, end, ...)                                                                               \
    ({                                                                                                                 \
        /* Plain mode (set by `tomo test`): emit only the message, no header or source echo, so a captured error */   \
        /* can be substring-matched without the echoed source (which includes the test's own `fails_compile` line) */ \
        /* producing false matches. */                                                                                \
        if (getenv("TOMO_PLAIN_ERRORS")) {                                                                             \
            fprint(stderr, __VA_ARGS__);                                                                               \
            exit(1);                                                                                                   \
        }                                                                                                              \
        file_t *_f = f;                                                                                                \
        if (USE_COLOR) fputs("\x1b[95;7;1m Compiler Error \x1b[m\n\n", stderr);                                        \
        else fputs("Compiler Error:\n\n", stderr);                                                                     \
        if (_f && start && end) {                                                                                      \
            highlight_error(_f, start, end, "\x1b[91;7;1m", 2, USE_COLOR);                                             \
            fputs("\n", stderr);                                                                                       \
        }                                                                                                              \
        if (getenv("TOMO_STACKTRACE")) {                                                                               \
            print_stacktrace(stderr, 1);                                                                               \
            fputs("\n\n", stderr);                                                                                     \
        }                                                                                                              \
        if (USE_COLOR) fputs("\x1b[91;1m", stderr);                                                                    \
        fprint(stderr, __VA_ARGS__, "\n");                                                                             \
        if (USE_COLOR) fputs("\x1b[m", stderr);                                                                        \
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
extern type_t *PRESENT_TYPE;
extern type_t *RESULT_TYPE;
