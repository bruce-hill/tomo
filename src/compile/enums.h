#pragma once

// Compilation of tagged unions (enums)

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_empty_enum(type_t *t);
Text_t compile_enum_constructors(env_t *env, ast_t *ast);
Text_t compile_enum_field_access(env_t *env, ast_t *ast);
Text_t compile_enum_header(env_t *env, ast_t *ast);
Text_t compile_enum_typeinfo(env_t *env, ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
