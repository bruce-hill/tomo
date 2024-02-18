#pragma once

#include <gc/cord.h>

#include "types.h"
#include "builtins/table.h"

typedef struct {
    CORD imports;
    CORD typedefs;
    CORD typecode;
    CORD staticdefs;
    CORD funcs;
    CORD typeinfos;
    CORD main;
} compilation_unit_t;

typedef struct {
    table_t *types, *globals, *locals;
    compilation_unit_t *code;
} env_t;

typedef struct {
    CORD code;
    type_t *type;
} binding_t;

env_t *new_compilation_unit(void);
env_t *fresh_scope(env_t *env);
__attribute__((noreturn))
void compiler_err(file_t *f, const char *start, const char *end, const char *fmt, ...);
binding_t *get_binding(env_t *env, const char *name);
void set_binding(env_t *env, const char *name, binding_t *binding);
#define code_err(ast, ...) compiler_err((ast)->file, (ast)->start, (ast)->end, __VA_ARGS__)

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
