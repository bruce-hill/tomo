// This file defines how to compile enums

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_empty_enum(type_t *t);
Text_t compile_enum_constructors(env_t *env, const char *name, tag_ast_t *tags);
Text_t compile_enum_field_access(env_t *env, ast_t *ast);
Text_t compile_enum_header(env_t *env, const char *name, tag_ast_t *tags);
Text_t compile_enum_typeinfo(env_t *env, const char *name, tag_ast_t *tags);
