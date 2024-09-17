#pragma once

// Compilation functions

#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "stdlib/util.h"
#include "environment.h"

CORD expr_as_text(env_t *env, CORD expr, type_t *t, CORD color);
CORD compile_file(env_t *env, ast_t *ast);
CORD compile_file_header(env_t *env, ast_t *ast);
CORD compile_declaration(type_t *t, const char *name);
CORD compile_type(type_t *t);
CORD compile(env_t *env, ast_t *ast);
void compile_namespace(env_t *env, const char *ns_name, ast_t *block);
CORD compile_namespace_header(env_t *env, const char *ns_name, ast_t *block);
CORD compile_statement(env_t *env, ast_t *ast);
CORD compile_statement_header(env_t *env, ast_t *ast);
CORD compile_type_info(env_t *env, type_t *t);
CORD compile_cli_arg_call(env_t *env, CORD fn_name, type_t *fn_type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
