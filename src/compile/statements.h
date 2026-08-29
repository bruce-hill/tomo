// This file defines how to compile statements

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_statement(env_t *env, ast_t *ast);
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code);
// In a `--debug` build, the `_$<name>$typeinfo` companion declaration that
// lets a debugger format the variable `_$<name>`; empty otherwise (see
// statements.c):
Text_t compile_debug_typeinfo(env_t *env, const char *name, type_t *t);
// The C text of an `InlineCCode` node, without the `#line` compile_statement
// would add (see statements.c):
Text_t compile_inline_c_code(env_t *env, ast_t *ast);
