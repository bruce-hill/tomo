#pragma once

// Compilation functions

#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "environment.h"
#include "stdlib/datatypes.h"

CORD expr_as_text(CORD expr, type_t *t, CORD color);
CORD compile_file(env_t *env, ast_t *ast);
CORD compile_file_header(env_t *env, Path_t header_path, ast_t *ast);
CORD compile_declaration(type_t *t, const char *name);
CORD compile_type(type_t *t);
CORD compile(env_t *env, ast_t *ast);
CORD compile_namespace_header(env_t *env, const char *ns_name, ast_t *block);
CORD compile_statement(env_t *env, ast_t *ast);
CORD compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast);
CORD compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast);
CORD compile_type_info(type_t *t);
CORD compile_cli_arg_call(env_t *env, CORD fn_name, type_t *fn_type, const char *version);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
