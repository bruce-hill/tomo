// This file defines how to compile structs

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../types.h"

Text_t compile_empty_struct(type_t *t);
Text_t compile_struct_typeinfo(env_t *env, type_t *t, const char *name, arg_ast_t *fields, bool is_secret,
                               bool is_opaque);
Text_t compile_struct_header(env_t *env, ast_t *ast);
Text_t compile_struct_field_access(env_t *env, ast_t *ast);
Text_t compile_struct_literal(env_t *env, ast_t *ast, type_t *t, arg_ast_t *args);
