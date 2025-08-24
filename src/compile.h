#pragma once

// Compilation functions

#include "ast.h"
#include "environment.h"
#include "stdlib/datatypes.h"
#include "types.h"

Text_t compile(env_t *env, ast_t *ast);
Text_t compile_cli_arg_call(env_t *env, Text_t fn_name, type_t *fn_type, const char *version);
Text_t compile_declaration(type_t *t, Text_t name);
Text_t compile_empty(type_t *t);
Text_t compile_file(env_t *env, ast_t *ast);
Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_lvalue(env_t *env, ast_t *ast);
Text_t compile_maybe_incref(env_t *env, ast_t *ast, type_t *t);
Text_t compile_namespace_header(env_t *env, const char *ns_name, ast_t *block);
Text_t compile_statement(env_t *env, ast_t *ast);
Text_t compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_type(type_t *t);
Text_t compile_type_info(type_t *t);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
