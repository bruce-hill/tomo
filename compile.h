#pragma once

#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "util.h"
#include "environment.h"

typedef struct {
    CORD header, c_file;
} module_code_t;

module_code_t compile_file(ast_t *ast);
CORD compile_type_ast(type_ast_t *t);
CORD compile_type(type_t *t);
CORD compile(env_t *env, ast_t *ast);
CORD compile_statement(env_t *env, ast_t *ast);
CORD compile_type_info(env_t *env, type_t *t);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
