// This file defines how to compile header files

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast);
Text_t compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast);
