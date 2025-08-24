#pragma once

// Compilation functions

#include <gc.h>

#include "environment.h"
#include "stdlib/datatypes.h"

Text_t compile(env_t *env, ast_t *ast);
Text_t compile_cli_arg_call(env_t *env, Text_t fn_name, type_t *fn_type, const char *version);
Text_t compile_declaration(type_t *t, Text_t name);
Text_t compile_empty(type_t *t);
Text_t compile_file(env_t *env, ast_t *ast);
Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_namespace_header(env_t *env, const char *ns_name, ast_t *block);
Text_t compile_statement(env_t *env, ast_t *ast);
Text_t compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_to_type(env_t *env, ast_t *ast, type_t *t);
Text_t compile_type(type_t *t);
Text_t compile_type_info(type_t *t);
Text_t expr_as_text(Text_t expr, type_t *t, Text_t color);
Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
