#pragma once

#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "builtins/util.h"
#include "environment.h"

typedef struct {
    const char *module_name;
    CORD header, c_file;
} module_code_t;

CORD expr_as_text(env_t *env, CORD expr, type_t *t, CORD color);
module_code_t compile_file(ast_t *ast);
CORD compile_type_ast(env_t *env, type_ast_t *t);
CORD compile_declaration(env_t *env, type_t *t, const char *name);
CORD compile_type(env_t *env, type_t *t);
CORD compile(env_t *env, ast_t *ast);
void compile_namespace(env_t *env, const char *ns_name, ast_t *block);
CORD compile_statement(env_t *env, ast_t *ast);
CORD compile_type_info(env_t *env, type_t *t);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
