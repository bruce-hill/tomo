// This file defines how to compile functions
#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_function_declaration(env_t *env, ast_t *ast);
Text_t compile_function_call(env_t *env, ast_t *ast);
Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args);
Text_t compile_lambda(env_t *env, ast_t *ast);
Table_t get_closed_vars(env_t *env, arg_ast_t *args, ast_t *block);
Text_t compile_function(env_t *env, Text_t name_code, ast_t *ast, Text_t *staticdefs);
Text_t compile_method_call(env_t *env, ast_t *ast);
